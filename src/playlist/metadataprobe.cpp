#include "metadataprobe.h"
#include "mediastate.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {
constexpr int probeTimeoutMs = 5000;

QJsonObject fileJson(const PlaylistFileInfo &file)
{
    return {{"canonical", file.canonicalPath}, {"bytes", file.bytes},
            {"modified", file.modified ? QJsonValue(*file.modified) : QJsonValue()},
            {"created", file.created ? QJsonValue(*file.created) : QJsonValue()}};
}

PlaylistFileInfo fileFromJson(const QJsonObject &object)
{
    const auto time = [&object](const char *key) -> std::optional<qint64> {
        const auto value = object.value(key);
        return value.isDouble() ? std::optional(value.toInteger()) : std::nullopt;
    };
    return {object.value("canonical").toString(), object.value("bytes").toInteger(-1), time("modified"), time("created")};
}

PlaylistFileInfo inspectFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) return {};
    const auto timestamp = [](const QDateTime &time) -> std::optional<qint64> {
        return time.isValid() ? std::optional(time.toMSecsSinceEpoch()) : std::nullopt;
    };
    return {info.canonicalFilePath().toCaseFolded(), info.size(),
            timestamp(info.lastModified()), timestamp(info.birthTime())};
}

QVariant property(mpv_handle *handle, const char *name)
{
    mpv_node value{};
    if (mpv_get_property(handle, name, MPV_FORMAT_NODE, &value) < 0) return {};
    const auto cleanup = qScopeGuard([&] { mpv_free_node_contents(&value); });
    return nodeValue(value);
}

QJsonObject readMedia(const QString &path)
{
    const QJsonObject unknown{{"read", false}};
    const std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> mpv(mpv_create(), mpv_terminate_destroy);
    if (!mpv) return unknown;
    const char *options[][2] = {
        {"config", "no"}, {"load-scripts", "no"}, {"osc", "no"}, {"ytdl", "no"},
        {"autoload-files", "no"}, {"access-references", "no"}, {"resume-playback", "no"}, {"idle", "yes"}, {"pause", "yes"},
        {"aid", "no"}, {"vid", "no"}, {"sid", "no"}, {"ao", "null"}, {"vo", "null"},
    };
    for (const auto &option : options)
        if (mpv_set_option_string(mpv.get(), option[0], option[1]) < 0) return unknown;
    if (mpv_initialize(mpv.get()) < 0 || mpv_hook_add(mpv.get(), 1, "on_preloaded", 0) < 0) return unknown;
    const auto utf8 = path.toUtf8();
    const char *load[]{"loadfile", utf8.constData(), nullptr};
    if (mpv_command_async(mpv.get(), 2, load) < 0) return unknown;

    for (;;) {
        const auto *event = mpv_wait_event(mpv.get(), -1);
        if (event->event_id == MPV_EVENT_END_FILE || event->event_id == MPV_EVENT_SHUTDOWN
            || (event->event_id == MPV_EVENT_COMMAND_REPLY && event->error < 0)) return unknown;
        if (event->event_id != MPV_EVENT_HOOK) continue;
        const auto id = static_cast<mpv_event_hook *>(event->data)->id;
        const auto release = qScopeGuard([&] { mpv_hook_continue(mpv.get(), id); });
        const auto tags = property(mpv.get(), "metadata").toMap();
        QString title;
        for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
            if (it.key().compare("title", Qt::CaseInsensitive) == 0) {
                title = it.value().toString().trimmed();
                break;
            }
        }
        QVariantMap video;
        for (const auto &entry : property(mpv.get(), "track-list").toList()) {
            const auto track = entry.toMap();
            if (track.value("type") != "video" || track.value("albumart").toBool()
                || track.value("image").toBool() || track.value("external").toBool()) continue;
            if (video.isEmpty() || (!video.value("default").toBool() && track.value("default").toBool())) video = track;
        }
        const double duration = property(mpv.get(), "duration").toDouble();
        return {{"read", true}, {"title", title}, {"width", video.value("demux-w").toInt()}, {"height", video.value("demux-h").toInt()},
                {"duration", std::isfinite(duration) && duration > 0 ? QJsonValue(duration) : QJsonValue()}};
    }
}

QJsonObject runRequest(const QJsonObject &request)
{
    if (request.value("check").isArray()) {
        const auto checks = request.value("check").toArray();
        QJsonArray changed;
        for (int i = 0; i < checks.size(); ++i) {
            const auto check = checks[i].toObject();
            const auto path = check.value("path").toString();
            if (QDir::isAbsolutePath(path) && inspectFile(path) != fileFromJson(check.value("file").toObject())) changed.append(i);
        }
        return {{"changed", changed}};
    }
    const auto path = request.value("path").toString();
    if (!QDir::isAbsolutePath(path)) return {};
    const auto before = inspectFile(path);
    QJsonObject result{{"file", fileJson(before)}};
    if (!request.value("media").toBool() || (before.versioned() && before == fileFromJson(request.value("cached").toObject())))
        return result;
    if (before.bytes < 0) {
        result["media"] = QJsonObject{{"read", false}};
        return result;
    }
    const auto media = readMedia(path);
    const auto after = inspectFile(path);
    if (before == after) result["media"] = media;
    else result["file"] = fileJson(after);
    return result;
}

MetadataProbe::Result parseResult(const QByteArray &line)
{
    const auto object = QJsonDocument::fromJson(line).object();
    MetadataProbe::Result result;
    if (object.value("file").isObject()) result.file = fileFromJson(object.value("file").toObject());
    const auto media = object.value("media").toObject();
    if (!media.isEmpty()) {
        const double duration = media.value("duration").toDouble();
        result.media = PlaylistMetadata{
            media.value("read").toBool() ? PlaylistMetadata::State::Read : PlaylistMetadata::State::Unknown,
            media.value("title").toString(), duration > 0 && std::isfinite(duration) ? std::optional(duration) : std::nullopt,
            QSize(media.value("width").toInt(), media.value("height").toInt())
        };
    }
    return result;
}
}

int MetadataProbe::runWorker()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto reply = QJsonDocument(runRequest(request)).toJson(QJsonDocument::Compact) + '\n';
        if (std::fwrite(reply.constData(), 1, reply.size(), stdout) != size_t(reply.size()) || std::fflush(stdout)) return 1;
    }
    return 0;
}

MetadataProbe::MetadataProbe(QObject *parent) : QObject(parent)
{
    timeout_.setSingleShot(true);
    timeout_.setTimerType(Qt::PreciseTimer);
    process_.setProgram(QCoreApplication::applicationFilePath());
    process_.setArguments({QString::fromLatin1(workerArgument)});
    process_.setStandardErrorFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= BELOW_NORMAL_PRIORITY_CLASS;
    });
#endif
    connect(&process_, &QProcess::started, this, [this] {
        if (stopping_) process_.kill();
        else send();
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &MetadataProbe::receive);
    connect(&process_, &QProcess::finished, this, [this] {
        process_.readAllStandardOutput();
        if (std::exchange(stopping_, false)) QTimer::singleShot(0, this, &MetadataProbe::start);
        else complete({});
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        stopping_ = false;
        Result result;
        result.failedToStart = true;
        complete(result);
    });
    connect(&timeout_, &QTimer::timeout, this, [this] {
        killWorker();
        complete({});
    });
}

MetadataProbe::~MetadataProbe()
{
    // QProcess reaps the terminated worker on destruction; it never waits for libmpv teardown.
    process_.disconnect(this);
    cancel();
}

quint64 MetadataProbe::read(const QString &path, bool media, const std::optional<PlaylistFileInfo> &cached)
{
    Q_ASSERT(!request_);
    QJsonObject object{{"path", path}, {"media", media}};
    if (cached) object["cached"] = fileJson(*cached);
    request_ = Request{nextRequest_++, path, QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n'};
    QTimer::singleShot(0, this, &MetadataProbe::start);
    return request_->id;
}

quint64 MetadataProbe::checkVersions(const QVector<Check> &checks)
{
    Q_ASSERT(!request_ && !checks.isEmpty());
    QJsonArray entries;
    for (const auto &check : checks) {
        if (check.file && check.file->bytes >= 0)
            entries.append(QJsonObject{{"path", check.path}, {"file", fileJson(*check.file)}});
        else entries.append(QJsonValue());
    }
    request_ = Request{nextRequest_++, {}, QJsonDocument(QJsonObject{{"check", entries}}).toJson(QJsonDocument::Compact) + '\n', int(checks.size())};
    QTimer::singleShot(0, this, &MetadataProbe::start);
    return request_->id;
}

void MetadataProbe::start()
{
    if (stopping_ || !request_ || timeout_.isActive()) return;
    timeout_.start(request_->checkCount ? 2 * probeTimeoutMs : probeTimeoutMs);
    if (process_.state() == QProcess::NotRunning) process_.start();
    else if (process_.state() == QProcess::Running) send();
}

void MetadataProbe::send()
{
    if (!request_) return;
    if (process_.write(request_->line) != request_->line.size()) {
        killWorker();
        complete({});
    }
}

void MetadataProbe::receive()
{
    if (!process_.canReadLine()) return;
    const auto line = process_.readLine();
    if (stopping_ || !request_) return;
    if (request_->checkCount) completeChecks(line);
    else complete(parseResult(line));
}

void MetadataProbe::cancel()
{
    request_.reset();
    timeout_.stop();
    killWorker();
}

void MetadataProbe::killWorker()
{
    if (!stopping_ && process_.state() != QProcess::NotRunning) {
        stopping_ = true;
        process_.kill();
    }
}

void MetadataProbe::complete(const Result &result)
{
    timeout_.stop();
    if (!request_) return;
    if (request_->checkCount) {
        completeChecks({});
        return;
    }
    auto request = std::exchange(request_, {});
    emit finished(request->id, request->path, result);
}

void MetadataProbe::completeChecks(const QByteArray &line)
{
    timeout_.stop();
    const auto request = std::exchange(request_, {});
    const auto changed = QJsonDocument::fromJson(line).object().value("changed");
    QVector<int> indexes;
    if (changed.isArray()) {
        for (const auto &value : changed.toArray()) {
            const auto i = value.toInt(-1);
            if (i >= 0 && i < request->checkCount) indexes.append(i);
        }
    }
    emit versionsChecked(request->id, indexes, changed.isArray());
}
