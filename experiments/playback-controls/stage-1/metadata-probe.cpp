#include "mediastate.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#include <windows.h>
#include <psapi.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

int runMetadataScenarios(const QString &fixtures, const QString &output);

namespace {
enum class Teardown { None, Stop, Destroy };

void emitEvent(const QJsonObject &event)
{
    const auto json = QJsonDocument(event).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, json.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void checkMpv(int status, const char *operation)
{
    if (status < 0)
        throw std::runtime_error(QString("%1: %2").arg(operation, mpv_error_string(status)).toStdString());
}

QVariant property(mpv_handle *mpv, const char *name)
{
    mpv_node value{};
    if (mpv_get_property(mpv, name, MPV_FORMAT_NODE, &value) < 0) return {};
    const auto cleanup = qScopeGuard([&] { mpv_free_node_contents(&value); });
    return nodeValue(value);
}

QJsonObject metadata(mpv_handle *mpv)
{
    const auto tags = property(mpv, "metadata").toMap();
    QString title;
    for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
        if (it.key().compare("title", Qt::CaseInsensitive) == 0) {
            title = it.value().toString().trimmed();
            break;
        }
    }

    const double duration = property(mpv, "duration").toDouble();
    const auto tracks = property(mpv, "track-list").toList();
    QVariantMap video;
    bool trackSelected = false;
    for (const auto &entry : tracks) {
        const auto track = entry.toMap();
        trackSelected |= track.value("selected").toBool();
        if (track.value("type") != "video" || track.value("albumart").toBool()
            || track.value("image").toBool() || track.value("external").toBool()) continue;
        if (video.isEmpty() || (!video.value("default").toBool() && track.value("default").toBool()))
            video = track;
    }
    const int width = video.value("demux-w").toInt();
    const int height = video.value("demux-h").toInt();
    return {
        {"title", title.isEmpty() ? QJsonValue() : QJsonValue(title)},
        {"mediaTitle", QJsonValue::fromVariant(property(mpv, "media-title"))},
        {"duration", std::isfinite(duration) && duration > 0 ? QJsonValue(duration) : QJsonValue()},
        {"width", width > 0 && height > 0 ? QJsonValue(width) : QJsonValue()},
        {"height", width > 0 && height > 0 ? QJsonValue(height) : QJsonValue()},
        {"fileFormat", QJsonValue::fromVariant(property(mpv, "file-format"))},
        {"rotation", QJsonValue::fromVariant(video.value("demux-rotation"))},
        {"sar", QJsonValue::fromVariant(video.value("demux-par"))},
        {"tracks", QJsonValue::fromVariant(tracks)},
        {"trackSelected", trackSelected},
        {"audioParams", QJsonValue::fromVariant(property(mpv, "audio-params"))},
        {"videoParams", QJsonValue::fromVariant(property(mpv, "video-params"))},
        {"audioOutput", QJsonValue::fromVariant(property(mpv, "current-ao"))},
        {"videoOutput", QJsonValue::fromVariant(property(mpv, "current-vo"))},
        {"paused", property(mpv, "pause").toBool()},
    };
}

QJsonObject readFile(const QString &file, Teardown teardown)
{
    QElapsedTimer elapsed;
    elapsed.start();
    std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> mpv(mpv_create(), mpv_terminate_destroy);
    if (!mpv) throw std::runtime_error("mpv_create failed");
    const char *options[][2] = {
        {"config", "no"}, {"load-scripts", "no"}, {"osc", "no"}, {"ytdl", "no"},
        {"autoload-files", "no"}, {"resume-playback", "no"}, {"idle", "yes"}, {"pause", "yes"},
        {"aid", "no"}, {"vid", "no"}, {"sid", "no"}, {"ao", "null"}, {"vo", "null"},
    };
    for (const auto &option : options)
        checkMpv(mpv_set_option_string(mpv.get(), option[0], option[1]), option[0]);
    checkMpv(mpv_initialize(mpv.get()), "mpv_initialize");
    checkMpv(mpv_hook_add(mpv.get(), 3, "on_preloaded", 0), "on_preloaded");
    const auto path = file.toUtf8();
    const char *load[]{"loadfile", path.constData(), nullptr};
    checkMpv(mpv_command_async(mpv.get(), 1, load), "loadfile");

    QJsonObject result{{"event", "result"}, {"status", "timeout"},
                       {"mpv", property(mpv.get(), "mpv-version").toString()}};
    qint64 teardownAt = -1;
    bool stopping = false;
    while (elapsed.elapsed() < 8000) {
        const auto *event = mpv_wait_event(mpv.get(), 0.02);
        if (event->event_id == MPV_EVENT_START_FILE) {
            emitEvent({{"event", "start-file"}, {"elapsedMs", elapsed.elapsed()}});
            if (teardown != Teardown::None) teardownAt = elapsed.elapsed() + 150;
        }
        if (event->event_id == MPV_EVENT_HOOK) {
            const auto id = static_cast<mpv_event_hook *>(event->data)->id;
            const auto release = qScopeGuard([&] { mpv_hook_continue(mpv.get(), id); });
            if (stopping) continue;
            result["metadata"] = metadata(mpv.get());
            result["status"] = "ok";
            break;
        }
        if (event->event_id == MPV_EVENT_COMMAND_REPLY) {
            if (event->reply_userdata == 2)
                emitEvent({{"event", "stop-reply"}, {"elapsedMs", elapsed.elapsed()}, {"error", event->error}});
            checkMpv(event->error, "command reply");
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto *end = static_cast<mpv_event_end_file *>(event->data);
            result["endReason"] = int(end->reason);
            result["mpvError"] = end->error;
            result["status"] = stopping ? "cancelled" : "unreadable";
            break;
        }
        if (teardownAt >= 0 && !stopping && elapsed.elapsed() >= teardownAt) {
            if (teardown == Teardown::Destroy) {
                emitEvent({{"event", "destroy-request"}, {"elapsedMs", elapsed.elapsed()}});
                result["status"] = "destroyed";
                break;
            }
            stopping = true;
            emitEvent({{"event", "stop-request"}, {"elapsedMs", elapsed.elapsed()}});
            const char *stop[]{"stop", nullptr};
            checkMpv(mpv_command_async(mpv.get(), 2, stop), "stop");
        }
    }

    const auto finished = elapsed.elapsed();
    mpv.reset();
    result["probeMs"] = finished;
    result["destroyMs"] = elapsed.elapsed() - finished;
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        result["peakWorkingSetKiB"] = qint64(memory.PeakWorkingSetSize / 1024);
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        result["cpuMs"] = double(ticks(kernel) + ticks(user)) / 10000;
    }
    return result;
}
}

int main(int argc, char **argv)
{
    std::setlocale(LC_NUMERIC, "C");
    if (argc > 1 && QByteArray(argv[1]) == "--read") {
        QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        if (args.size() != 4) return 2;
        Teardown teardown = Teardown::None;
        if (args[3] == "stop") teardown = Teardown::Stop;
        else if (args[3] == "destroy") teardown = Teardown::Destroy;
        else if (args[3] != "none") return 2;
        try {
            emitEvent(readFile(args[2], teardown));
            return 0;
        } catch (const std::exception &error) {
            emitEvent({{"event", "result"}, {"status", "error"}, {"message", QString::fromUtf8(error.what())}});
            return 1;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("zopana metadata probe");
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    return runMetadataScenarios(args[1], args[2]);
}
