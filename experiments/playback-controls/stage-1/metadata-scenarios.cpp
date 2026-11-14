#include "mainwindow.h"
#include "videowidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool passed, const QString &message)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", message.toUtf8().constData());
    std::fflush(stdout);
    if (!passed) throw std::runtime_error(message.toStdString());
}

bool until(auto predicate, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate()) {
        if (clock.elapsed() >= timeoutMs) return false;
        QTest::qWait(5);
    }
    return true;
}

class Child
{
public:
    void start(const QString &path, const QString &teardown = "none")
    {
        process_.setCreateProcessArgumentsModifier([](auto *args) { args->flags |= CREATE_NO_WINDOW; });
        QObject::connect(&process_, &QProcess::finished, &process_, [this] { exitMs_ = clock_.elapsed(); });
        clock_.start();
        process_.start(QCoreApplication::applicationFilePath(), {"--read", path, teardown});
    }

    QJsonObject event(const QString &name)
    {
        buffer_ += process_.readAllStandardOutput();
        for (auto newline = buffer_.indexOf('\n'); newline >= 0; newline = buffer_.indexOf('\n')) {
            const auto next = QJsonDocument::fromJson(buffer_.left(newline)).object();
            if (next.isEmpty()) throw std::runtime_error("Invalid child output");
            events_.append(next);
            buffer_.remove(0, newline + 1);
        }
        for (const auto &value : events_) {
            const auto entry = value.toObject();
            if (entry["event"] == name) return entry;
        }
        return {};
    }

    bool finished() const { return process_.state() == QProcess::NotRunning; }
    bool normalExit() const { return process_.exitStatus() == QProcess::NormalExit && process_.exitCode() == 0; }
    void kill() { process_.kill(); }
    qint64 elapsedMs() const { return clock_.elapsed(); }
    qint64 exitMs() const { return exitMs_; }

    QJsonObject evidence()
    {
        event("result");
        return {{"elapsedMs", exitMs_ < 0 ? clock_.elapsed() : exitMs_}, {"exitCode", process_.exitCode()},
                {"normalExit", normalExit()}, {"events", events_},
                {"stderr", QString::fromUtf8(process_.readAllStandardError())}};
    }

private:
    QElapsedTimer clock_;
    QByteArray buffer_;
    QJsonArray events_;
    qint64 exitMs_ = -1;
    QProcess process_;
};

class StalledPipe
{
public:
    StalledPipe() : path_(QStringLiteral("\\\\.\\pipe\\zopana-metadata-") + QUuid::createUuid().toString(QUuid::Id128)) {}
    ~StalledPipe() { close(); }

    bool listen()
    {
        pipe_ = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(path_.utf16()),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 0, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        connect_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!connect_.hEvent) return false;
        if (ConnectNamedPipe(pipe_, &connect_)) return SetEvent(connect_.hEvent);
        const auto error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) return SetEvent(connect_.hEvent);
        pending_ = error == ERROR_IO_PENDING;
        return pending_;
    }

    QString path() const { return path_; }
    bool connected() const { return WaitForSingleObject(connect_.hEvent, 0) == WAIT_OBJECT_0; }

    void close()
    {
        if (pipe_ != INVALID_HANDLE_VALUE) {
            if (pending_ && !connected()) {
                CancelIoEx(pipe_, &connect_);
                DWORD unused;
                GetOverlappedResult(pipe_, &connect_, &unused, TRUE);
            }
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        if (connect_.hEvent) {
            CloseHandle(connect_.hEvent);
            connect_.hEvent = nullptr;
        }
    }

private:
    QString path_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    OVERLAPPED connect_{};
    bool pending_ = false;
};

void readSample(const QDir &fixtures, const QJsonObject &expected, QJsonArray &samples)
{
    const auto file = expected["file"].toString();
    Child child;
    child.start(fixtures.filePath(file));
    const bool finished = until([&] { return child.finished(); }, 3500);
    const auto result = child.event("result");
    auto evidence = child.evidence();
    evidence["file"] = file;
    samples.append(evidence);
    require(finished && child.normalExit() && result["status"] == expected["status"], file + " probe outcome");
    if (expected["status"] != "ok") {
        require(!result.contains("metadata"), file + " has no published metadata");
        return;
    }

    const auto metadata = result["metadata"].toObject();
    require(metadata["title"] == expected["title"] && metadata["width"] == expected["width"]
                && metadata["height"] == expected["height"], file + " title and coded dimensions");
    const auto actual = metadata["duration"];
    const auto reference = expected["duration"];
    const bool matchingDuration = reference.isNull() ? actual.isNull()
        : actual.isDouble() && std::abs(actual.toDouble() - reference.toDouble()) <= 0.10;
    require(matchingDuration, file + " duration agrees with FFprobe or is unknown");
    if (file == "rotated-sar.mp4")
        require(metadata["rotation"].toInt() == 90 && std::abs(metadata["sar"].toDouble() - 4.0/3.0) < .001,
                "rotation and SAR are independent of the coded dimensions");
    require(metadata["paused"].toBool() && !metadata["trackSelected"].toBool()
                && metadata["audioParams"].isNull() && metadata["videoParams"].isNull()
                && metadata["audioOutput"].isNull() && metadata["videoOutput"].isNull(),
            file + " has no selected track, playback parameters or output");
}

void stalledRead(const QString &action, QJsonArray &stalls)
{
    Child child;
    StalledPipe pipe;
    require(pipe.listen(), "local pipe listening");
    const bool stop = action == "stop";
    const bool destroy = action == "destroy";
    const bool cooperative = stop || destroy;
    child.start(pipe.path(), cooperative ? action : "none");
    require(until([&] { return pipe.connected() && !child.event("start-file").isEmpty(); }, 3000),
            action + " reached mpv file input");

    if (stop)
        require(until([&] { return !child.event("stop-reply").isEmpty(); }, 2000)
                    && child.event("stop-reply")["error"].toInt(-1) == 0, "mpv acknowledges stop");
    if (destroy)
        require(until([&] { return !child.event("destroy-request").isEmpty(); }, 2000), "mpv teardown entered");
    const int holdMs = action == "timeout" ? 600 : 400;
    QElapsedTimer blocked;
    blocked.start();
    const bool stoppedWhileBlocked = until([&] { return child.finished() || !child.event("result").isEmpty(); }, holdMs);
    const auto observedBlockedMs = blocked.elapsed();
    const auto cleanupStarted = child.elapsedMs();
    if (!stoppedWhileBlocked) {
        if (cooperative) pipe.close();
        else child.kill();
    }
    const bool reaped = until([&] { return child.finished(); }, 2000);
    const auto result = child.event("result");
    auto evidence = child.evidence();
    evidence["action"] = action;
    evidence["blockedWaitMs"] = holdMs;
    evidence["observedBlockedMs"] = observedBlockedMs;
    evidence["stoppedWhileBlocked"] = stoppedWhileBlocked;
    evidence["reaped"] = reaped;
    evidence["cleanupMs"] = reaped && !stoppedWhileBlocked ? QJsonValue(child.exitMs() - cleanupStarted) : QJsonValue();
    stalls.append(evidence);

    require(reaped, action + " reaps the worker");
    if (cooperative) {
        require(child.normalExit() && result["status"] == (stop ? "cancelled" : "destroyed")
                    && !result.contains("metadata"), action + " discards the interrupted probe");
        std::printf("OBS %s %s\n", action.toUtf8().constData(), stoppedWhileBlocked
                    ? "completed with input still open" : "completed after the pipe closed");
        std::fflush(stdout);
    } else {
        require(!stoppedWhileBlocked, action + " remains pending at the end of the I/O hold");
        require(!child.normalExit() && result.isEmpty(), action + " publishes no metadata");
    }
}

struct PlaybackSample
{
    qint64 wallMs;
    double position;
    int swaps;
    int decoderDrops;
    int outputDrops;
    QString ao;
    bool hasAudioOutput;

    QJsonObject json() const
    {
        return {{"wallMs", wallMs}, {"position", position}, {"frameSwaps", swaps}, {"decoderDrops", decoderDrops},
                {"outputDrops", outputDrops}, {"audioDriver", ao}, {"hasAudioOutput", hasAudioOutput}};
    }
};

void playbackScenarios(const QDir &fixtures, const QJsonArray &cases, QJsonObject &report)
{
    Player player;
    require(player.initialize(), "foreground Player initialized");
    MainWindow window(player);
    window.resize(800, 540);
    window.show();
    auto *video = window.findChild<VideoWidget *>();
    require(video && until([&] { return video->rendererReady(); }, 5000), "foreground renderer ready");
    report["windowDpr"] = video->devicePixelRatioF();
    report["screen"] = window.screen()->name();
    player.setMuted(true);
    player.openFile(fixtures.filePath("playing.mp4"), 42);
    require(until([&] { return player.state() == Player::State::Loaded && player.position() > 0.5; }, 6000),
            "foreground video is playing");

    int swaps = 0;
    int unexpectedRequests = 0;
    QObject::connect(video, &VideoWidget::frameSwapped, &window, [&] { ++swaps; });
    QObject::connect(&player, &Player::requestSubmitted, &window, [&](auto, auto, auto, const QString &op) {
        if (op.startsWith("loadfile") || op.startsWith("seek") || op.startsWith("stop")) ++unexpectedRequests;
    });
    QElapsedTimer clock;
    clock.start();
    auto sample = [&] {
        const auto drops = [&](const char *name) {
            int64_t n = 0;
            return mpv_get_property(player.handle(), name, MPV_FORMAT_INT64, &n) >= 0 ? int(n) : -1;
        };
        auto *ao = mpv_get_property_string(player.handle(), "current-ao");
        const auto name = QString::fromUtf8(ao);
        if (ao) mpv_free(ao);
        mpv_node audio{};
        const bool hasAudio = mpv_get_property(player.handle(), "audio-out-params", MPV_FORMAT_NODE, &audio) >= 0;
        if (hasAudio) mpv_free_node_contents(&audio);
        return PlaybackSample{clock.elapsed(), player.position(), swaps, drops("decoder-frame-drop-count"),
                              drops("frame-drop-count"), name, hasAudio};
    };
    const auto before = sample();
    const auto generation = player.generation();
    const auto epoch = player.renderEpoch();
    const auto path = player.path();
    const auto context = video->context();
    const auto itemId = player.itemId();
    QTest::qWait(500);
    const auto baseline = sample();
    require(baseline.position > before.position + 0.25 && baseline.swaps > before.swaps
                && baseline.hasAudioOutput && !baseline.ao.isEmpty(), "baseline advances with video and audio output");

    QTimer pulse;
    pulse.setTimerType(Qt::PreciseTimer);
    qint64 lastPulse = clock.elapsed();
    qint64 maxGap = 0;
    int ticks = 0;
    QObject::connect(&pulse, &QTimer::timeout, &window, [&] {
        const auto now = clock.elapsed();
        maxGap = std::max(maxGap, now - lastPulse);
        lastPulse = now;
        ++ticks;
    });
    pulse.start(20);

    QJsonArray samples, stalls;
    const auto writeEvidence = [&] {
        report["samples"] = samples;
        report["stalls"] = stalls;
    };
    try {
        for (const auto &entry : cases) readSample(fixtures, entry.toObject(), samples);
        for (int run = 0; run < 3; ++run)
            for (const QString action : {"stop", "destroy", "cancel", "timeout"}) stalledRead(action, stalls);
        readSample(fixtures, cases.first().toObject(), samples);
    } catch (...) {
        writeEvidence();
        throw;
    }
    writeEvidence();
    pulse.stop();
    const auto after = sample();
    const auto elapsed = after.wallMs - baseline.wallMs;
    const auto positionAdvance = after.position - baseline.position;
    report["foreground"] = QJsonObject{
        {"before", before.json()}, {"afterBaseline", baseline.json()}, {"afterProbes", after.json()},
        {"itemIdBefore", qint64(itemId)}, {"itemIdAfter", qint64(player.itemId())},
        {"generationBefore", qint64(generation)}, {"generationAfter", qint64(player.generation())},
        {"renderEpochBefore", qint64(epoch)}, {"renderEpochAfter", qint64(player.renderEpoch())},
        {"sameGlContext", video->context() == context}, {"samePath", player.path() == path},
        {"paused", player.paused()}, {"unexpectedPlaybackRequests", unexpectedRequests},
        {"pulseCount", ticks}, {"maxPulseGapMs", maxGap}, {"probeIntervalMs", elapsed},
        {"positionAdvance", positionAdvance},
    };
    require(player.state() == Player::State::Loaded && !player.paused() && player.itemId() == 42
                && player.generation() == generation && player.renderEpoch() == epoch && player.path() == path
                && video->context() == context && unexpectedRequests == 0, "foreground session survives all workers");
    require(positionAdvance >= elapsed / 1000.0 - 0.5 && after.swaps > baseline.swaps && ticks > 0
                && after.hasAudioOutput && after.ao == baseline.ao, "foreground playback advances throughout probing");
}
}

int runMetadataScenarios(const QString &fixtures, const QString &output)
{
    const QDir dir(fixtures);
    QFile manifest(dir.filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    const auto cases = QJsonDocument::fromJson(manifest.readAll()).object()["cases"].toArray();
    if (cases.isEmpty()) return 2;
    QJsonObject report{{"qt", qVersion()}, {"os", QSysInfo::prettyProductName()},
        {"kernelVersion", QSysInfo::kernelVersion()}, {"mpvApi", QString::number(mpv_client_api_version() >> 16) + "."
            + QString::number(mpv_client_api_version() & 0xffff)}};
    bool passed = false;
    try {
        playbackScenarios(dir, cases, report);
        passed = true;
    } catch (const std::exception &error) {
        report["failure"] = QString::fromUtf8(error.what());
    }
    report["passed"] = passed;
    QSaveFile evidence(QDir(output).filePath("results.json"));
    if (!evidence.open(QIODevice::WriteOnly)) return 2;
    const auto json = QJsonDocument(report).toJson();
    if (evidence.write(json) != json.size() || !evidence.commit()) return 2;
    return passed ? 0 : 1;
}
