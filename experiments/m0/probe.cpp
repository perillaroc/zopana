#include "mainwindow.h"
#include "player.h"
#include "videowidget.h"
#include "historystore.h"
#include "sessioncontroller.h"
#include "settingsstore.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QOpenGLContext>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <windows.h>
#include <psapi.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
QJsonValue jsonNode(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_FLAG: return bool(node.u.flag);
    case MPV_FORMAT_INT64: return double(node.u.int64);
    case MPV_FORMAT_DOUBLE: return node.u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QJsonArray array;
        for (int i = 0; i < node.u.list->num; ++i) array.append(jsonNode(node.u.list->values[i]));
        return array;
    }
    case MPV_FORMAT_NODE_MAP: {
        QJsonObject object;
        for (int i = 0; i < node.u.list->num; ++i)
            object[QString::fromUtf8(node.u.list->keys[i])] = jsonNode(node.u.list->values[i]);
        return object;
    }
    default: return QJsonValue();
    }
}

bool waitFor(const std::function<bool()> &condition, int timeout = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

double cpuMilliseconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto value = [](FILETIME time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return double(value(kernel) + value(user)) / 10000.0;
}

QJsonObject memory()
{
    PROCESS_MEMORY_COUNTERS_EX info{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&info), sizeof(info));
    return {{"workingSetMiB", double(info.WorkingSetSize) / 1048576},
            {"privateMiB", double(info.PrivateUsage) / 1048576}};
}

class Surface final : public QWidget
{
public:
    VideoWidget *video = nullptr;
    QLabel *osd;
    QPushButton *notice;
    QWidget *bar;
    int noticeClicks = 0;

    Surface()
    {
        osd = new QLabel("M0 OSD / zoom 2x", this);
        osd->setStyleSheet("background:#8a176b;color:white;padding:8px;font-size:18px");
        osd->setAttribute(Qt::WA_TransparentForMouseEvents);
        notice = new QPushButton("M0 interactive notice", this);
        connect(notice, &QPushButton::clicked, this, [this] { ++noticeClicks; });
        bar = new QWidget(this);
        bar->setStyleSheet("background:#293951");
        auto *label = new QLabel("M0 floating controls / fixed viewport", bar);
        label->setStyleSheet("color:white;font-size:18px");
        label->setGeometry(16, 12, 600, 40);
    }

    void arrange()
    {
        if (video) video->setGeometry(rect());
        osd->setGeometry(20, 20, 260, 44);
        notice->setGeometry(width() - 260, 20, 240, 44);
        bar->setGeometry(0, height() - 72, width(), 72);
        osd->raise(); notice->raise(); bar->raise();
    }
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); arrange(); }
};

class Probe final : public QObject
{
public:
    Player player;
    std::unique_ptr<QMainWindow> window;
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<HistoryStore> history;
    std::unique_ptr<SessionController> session;
    Surface *surface = nullptr;
    VideoWidget *video = nullptr;
    mpv_handle *client = nullptr;
    Player::State state = Player::State::Idle;
    QString path;
    QString fixtures, output;
    int generation = 0, loadedCount = 0, rendererReadyCount = 0, toggles = 0;
    QJsonArray checks, replies, events;
    QJsonObject details;
    QElapsedTimer clock;

    struct Request {
        int generation;
        qint64 submitted;
        QString operation;
        bool done = false;
        int error = 0;
        QJsonValue result;
    };
    std::map<uint64_t, Request> pending;
    uint64_t nextRequest = 100;
    QTimer pump;

    Probe(QString fixtureDir, QString outputDir, bool baseline, bool production = false)
        : fixtures(std::move(fixtureDir)), output(std::move(outputDir))
    {
        fixtures = QDir::fromNativeSeparators(fixtures);
        output = QDir::fromNativeSeparators(output);
        QDir().mkpath(output);
        clock.start();
        if (!player.initialize()) throw std::runtime_error("Player initialization failed");
        client = mpv_create_client(player.handle(), "m0-observer");
        if (!client) throw std::runtime_error("Observer client initialization failed");
        // Separate client, same playback core: probe replies cannot underflow Player's request counters.
        pump.setInterval(5);
        connect(&pump, &QTimer::timeout, this, [this] { drain(); });
        pump.start();
        if (baseline) {
            auto main = std::make_unique<MainWindow>(player);
            if (production) {
                settings = std::make_unique<SettingsStore>(output + "/profile/config");
                history = std::make_unique<HistoryStore>(output + "/profile/data");
                session = std::make_unique<SessionController>(player, *main, *settings, *history);
            }
            window = std::move(main);
            window->resize(960, 640); // Keep the B04 comparison size across UI milestones.
            video = window->findChild<VideoWidget *>("videoWidget");
        } else {
            window = std::make_unique<QMainWindow>();
            surface = new Surface;
            window->setCentralWidget(surface);
            makeVideo();
            window->resize(960, 640);
            window->setWindowTitle("zopana M0 render probe");
            connect(&player, &Player::videoSizeChanged, this, [this](QSize size) { video->setVideoSize(size); });
            connect(&player, &Player::fileOpening, this, [this] { video->setVideoSize({}); });
        }
        connect(&player, &Player::mediaChanged, this, [this](Player::State s, const QString &p, const QString &error) {
            state = s; path = p;
            if (s == Player::State::Loaded) ++loadedCount;
            events.append(QJsonObject{{"type", "Player state"}, {"state", int(s)}, {"path", p},
                                      {"generation", generation}, {"ms", double(clock.elapsed())}, {"error", error}});
        });
        window->show(); window->raise(); window->activateWindow();
        if (!waitFor([this] { return video->rendererReady(); })) throw std::runtime_error("Renderer unavailable");
        details["qt"] = qVersion();
        details["mpvVersion"] = property("mpv-version");
        details["apiVersion"] = double(mpv_client_api_version());
        details["devicePixelRatio"] = video->devicePixelRatioF();
        details["screenLogicalDpi"] = window->screen()->logicalDotsPerInch();
        details["screenPhysicalPixels"] = QString("%1x%2").arg(window->screen()->size().width()).arg(window->screen()->size().height());
        command({"set", "sub-auto", "no"});
        command({"set", "osd-level", "0"});
    }

    ~Probe() override
    {
        pump.stop();
        if (session) window->close();
        session.reset();
        player.shutdown();
        window.reset();
        if (client) mpv_destroy(client);
    }

    void makeVideo()
    {
        video = new VideoWidget(player.handle(), surface);
        surface->video = video;
        connect(video, &VideoWidget::rendererReadyChanged, this, [this](bool ready) {
            if (ready) ++rendererReadyCount;
            player.setRendererReady(ready);
        });
        connect(video, &VideoWidget::playbackToggleRequested, this, [this] { ++toggles; });
        surface->arrange(); video->show(); surface->arrange();
    }

    QJsonValue property(const char *name)
    {
        mpv_node node{};
        if (mpv_get_property(client, name, MPV_FORMAT_NODE, &node) < 0) return QJsonValue();
        const auto result = jsonNode(node);
        mpv_free_node_contents(&node);
        return result;
    }

    void drain()
    {
        for (;;) {
            const auto &event = *mpv_wait_event(client, 0);
            if (event.event_id == MPV_EVENT_NONE) return;
            if (event.event_id == MPV_EVENT_COMMAND_REPLY) {
                const auto found = pending.find(event.reply_userdata);
                if (found == pending.end()) continue;
                auto &request = found->second;
                request.done = true;
                request.error = event.error;
                if (event.data) request.result = jsonNode(static_cast<mpv_event_command *>(event.data)->result);
                replies.append(QJsonObject{{"requestId", double(event.reply_userdata)}, {"operation", request.operation},
                    {"requestGeneration", request.generation}, {"currentGeneration", generation},
                    {"stale", request.generation != generation}, {"error", request.error},
                    {"elapsedMs", double(clock.elapsed() - request.submitted)}, {"result", request.result}});
            } else if (event.event_id == MPV_EVENT_START_FILE) {
                const auto *start = static_cast<mpv_event_start_file *>(event.data);
                events.append(QJsonObject{{"type", "START_FILE"}, {"entryId", double(start->playlist_entry_id)}, {"ms", double(clock.elapsed())}});
            } else if (event.event_id == MPV_EVENT_END_FILE) {
                const auto *end = static_cast<mpv_event_end_file *>(event.data);
                events.append(QJsonObject{{"type", "END_FILE"}, {"entryId", double(end->playlist_entry_id)},
                    {"reason", int(end->reason)}, {"ms", double(clock.elapsed())}});
            }
        }
    }

    uint64_t submit(const QStringList &arguments)
    {
        std::vector<QByteArray> storage;
        for (const auto &argument : arguments) storage.push_back(argument.toUtf8());
        std::vector<const char *> args;
        for (const auto &argument : storage) args.push_back(argument.constData());
        args.push_back(nullptr);
        const auto id = ++nextRequest;
        pending.emplace(id, Request{generation, clock.elapsed(), arguments.join(' ')});
        const int error = mpv_command_async(client, id, args.data());
        if (error < 0) { pending.at(id).done = true; pending.at(id).error = error; }
        return id;
    }

    bool command(const QStringList &args)
    {
        const auto id = submit(args);
        return waitFor([&] { return pending.at(id).done; }) && pending.at(id).error >= 0;
    }

    void check(QString name, bool passed, QJsonObject data = {})
    {
        data["name"] = name; data["passed"] = passed;
        checks.append(data);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData());
        std::fflush(stdout);
    }

    void require(QString name, bool passed, QJsonObject data = {})
    {
        check(name, passed, data);
        if (!passed) throw std::runtime_error(name.toStdString());
    }

    void open(const QString &name)
    {
        ++generation;
        const QString target = fixtures + '/' + name;
        player.openFile(target);
        require("file loaded: " + name, waitFor([&] { return state == Player::State::Loaded && path == target; }));
        require("video configured", waitFor([this] { return property("vo-configured").toBool(); }));
        QTest::qWait(200);
    }

    void pauseAt(double seconds)
    {
        player.setPaused(true);
        require("pause", waitFor([this] { return property("pause").toBool(); }));
        player.seek(seconds);
        require("seek paused", waitFor([&] { return std::abs(property("time-pos").toDouble(-100) - seconds) < .08; }));
        QTest::qWait(200);
    }

    QImage shot(const QString &name)
    {
        QElapsedTimer capture; capture.start();
        auto image = video->grabFramebuffer();
        const double readbackMs = double(capture.nsecsElapsed()) / 1000000.0;
        auto timings = details["viewportReadbacks"].toArray();
        timings.append(QJsonObject{{"file", name}, {"readbackMs", readbackMs}});
        details["viewportReadbacks"] = timings;
        require("viewport save: " + name, !image.isNull() && image.save(output + '/' + name));
        return image;
    }

    void windowShot(const QString &name) { window->grab().save(output + '/' + name); }

    void zoom(int steps)
    {
        const QPointF point(video->rect().center());
        QWheelEvent event(point, video->mapToGlobal(point.toPoint()), {}, {0, steps * 120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(video, &event);
        QTest::qWait(180);
    }

    QJsonObject snapshot()
    {
        QJsonObject object;
        for (const auto *key : {"time-pos", "pause", "speed", "aid", "sid", "sub-visibility", "sub-scale",
                                "sub-delay", "audio-delay", "ab-loop-a", "ab-loop-b", "track-list"})
            object[key] = property(key);
        return object;
    }

    void baseline(bool performance)
    {
        windowShot("baseline-idle.png");
        open(QString::fromUtf8("网格 multi.mkv"));
        pauseAt(1.2);
        windowShot("baseline-feature-window.png");
        window->findChild<QAction *>("view.mode.standard")->trigger();
        windowShot("baseline-standard-window.png");
        QTest::keyClick(window.get(), Qt::Key_F11);
        QTest::qWait(300);
        windowShot("baseline-standard-fullscreen.png");
        window->findChild<QAction *>("view.mode.feature")->trigger();
        windowShot("baseline-feature-fullscreen.png");
        QTest::keyClick(window.get(), Qt::Key_F11);
        QTest::qWait(150);
        if (!performance) return;
        QJsonArray metrics;
        for (const auto &media : {QString("motion-1080.mp4"), QString("motion-4k.mp4")}) {
            double firstFrameMs = -1;
            QElapsedTimer firstFrame;
            auto connection = connect(video, &QOpenGLWidget::frameSwapped, this, [&] {
                if (firstFrameMs < 0 && state == Player::State::Loaded && property("time-pos").isDouble())
                    firstFrameMs = double(firstFrame.elapsed());
            });
            firstFrame.start();
            open(media);
            waitFor([&] { return firstFrameMs >= 0; });
            disconnect(connection);
            QTest::qWait(1000);
            const auto droppedBefore = property("frame-drop-count");
            const auto decoderBefore = property("decoder-frame-drop-count");
            const double cpuStart = cpuMilliseconds();
            QElapsedTimer elapsed; elapsed.start();
            QJsonArray samples;
            for (int i = 0; i < 24; ++i) { QTest::qWait(250); samples.append(memory()); }
            SYSTEM_INFO info{}; GetSystemInfo(&info);
            const double cpu = (cpuMilliseconds() - cpuStart) / elapsed.elapsed() * 100;
            QJsonObject metric{{"media", media}, {"firstFrameMs", firstFrameMs}, {"sampleMs", double(elapsed.elapsed())},
                {"cpuPercentOneCore", cpu}, {"cpuPercentMachine", cpu / info.dwNumberOfProcessors},
                {"logicalProcessors", int(info.dwNumberOfProcessors)}, {"memorySamples", samples},
                {"frameDropBefore", droppedBefore}, {"frameDropAfter", property("frame-drop-count")},
                {"decoderDropBefore", decoderBefore}, {"decoderDropAfter", property("decoder-frame-drop-count")},
                {"videoParams", property("video-params")}, {"hwdec", property("hwdec-current")}};
            metrics.append(metric);
            check("baseline performance collected: " + media, firstFrameMs >= 0 && droppedBefore.isDouble());
        }
        details["performance"] = metrics;
    }

    void renderCases()
    {
        open(QString::fromUtf8("网格 multi.mkv"));
        pauseAt(1.2);
        check("M0.5 seekable", property("seekable").toBool());
        details["initialTracks"] = property("track-list");
        QJsonObject capabilities;
        for (const auto *key : {"seekable", "video-params", "audio-params", "audio-out-params", "sub-scale", "audio-pitch-correction"})
            capabilities[key] = property(key);
        details["capabilities"] = capabilities;
        command({"set", "sub-visibility", "no"}); QTest::qWait(150);
        const auto original = shot("viewport-fit-off.png");
        require("M0.3 source screenshot", command({"screenshot-to-file", output + "/source.png", "video"}));
        const QImage source(output + "/source.png");
        check("M0.3 source dimensions", source.size() == QSize(960, 540));
        check("M0.3 physical viewport dimensions", original.size() == QSize(qRound(video->width() * video->devicePixelRatioF()), qRound(video->height() * video->devicePixelRatioF())));
        details["viewportLogicalSize"] = QString("%1x%2").arg(video->width()).arg(video->height());
        details["viewportPhysicalSize"] = QString("%1x%2").arg(original.width()).arg(original.height());

        video->setInputMode(VideoWidget::InputMode::Standard);
        // Test Qt's hit-test hierarchy directly; desktop occlusion is unrelated
        // to sibling stacking and makes QApplication::widgetAt nondeterministic.
        windowShot("overlay-before-input.png");
        auto *hit = surface->childAt(surface->osd->geometry().center());
        require("M0.1 OSD pointer passes to video", hit == video,
                {{"hitClass", hit ? hit->metaObject()->className() : "null"}});
        QTest::mouseClick(hit, Qt::LeftButton, Qt::NoModifier, hit->mapFrom(surface, surface->osd->geometry().center()));
        waitFor([&] { return toggles == 1; }, QApplication::doubleClickInterval() + 200);
        check("M0.1 exactly one video toggle", toggles == 1);
        hit = surface->childAt(surface->notice->geometry().center());
        QTest::mouseClick(hit, Qt::LeftButton);
        check("M0.1 notice consumes click", surface->noticeClicks == 1 && toggles == 1);
        hit = surface->childAt(surface->bar->mapTo(surface, QPoint(700, 30)));
        check("M0.1 controls hit area", hit == surface->bar);
        windowShot("overlay-window.png");
        const auto covered = shot("viewport-overlay-excluded.png");
        const auto guiThread = std::this_thread::get_id();
        auto encoding = std::async(std::launch::async, [image = covered.copy(), destination = output + "/viewport-worker.png", guiThread] {
            return std::pair(image.save(destination), std::this_thread::get_id() != guiThread);
        });
        require("M0.3 worker PNG completes", waitFor([&] { return encoding.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; }));
        const auto encoded = encoding.get();
        // PNG reloads may choose a different QImage storage format. Pixel
        // equivalence is checked independently in analyze.py, not by operator==.
        check("M0.3 PNG encoding off GUI thread", encoded.first && encoded.second,
              {{"saved", encoded.first}, {"offGuiThread", encoded.second}});
        surface->osd->hide(); surface->notice->hide(); surface->bar->hide();
        QTest::qWait(100);
        check("M0.1/M0.3 overlays absent from FBO", covered == video->grabFramebuffer());
        surface->osd->show(); surface->notice->show(); surface->bar->show();
        QMenu menu(window.get());
        auto *action = menu.addAction("M0 menu action");
        int menuClicks = 0;
        connect(action, &QAction::triggered, this, [&] { ++menuClicks; });
        menu.popup(video->mapToGlobal(QPoint(350, 200)));
        QTest::qWait(150);
        windowShot("overlay-menu.png");
        menu.grab().save(output + "/overlay-menu-popup.png");
        check("M0.1 popup above GL", menu.isVisible() && menu.isWindow() && menu.activeAction() == nullptr);
        QTest::mouseClick(&menu, Qt::LeftButton, Qt::NoModifier, menu.actionGeometry(action).center());
        check("M0.1 popup action only", menuClicks == 1 && toggles == 1);
        video->setInputMode(VideoWidget::InputMode::Feature);

        auto *context = video->context();
        const int loads = loadedCount, contexts = rendererReadyCount;
        const QSize normal = window->size();
        bool stableViewport = true;
        zoom(5);
        for (int i = 0; i < 20; ++i) {
            window->showFullScreen(); QTest::qWait(35);
            const auto size = video->size();
            surface->bar->hide(); QTest::qWait(10);
            stableViewport &= size == video->size();
            surface->bar->show();
            if (i == 0) { windowShot("overlay-fullscreen.png"); shot("viewport-fullscreen-zoom.png"); }
            window->showNormal(); window->resize(normal); QTest::qWait(35);
        }
        check("M0.2 20 fullscreen cycles preserve context", video->context() == context && rendererReadyCount == contexts && loadedCount == loads,
              {{"readySignalsBefore", contexts}, {"readySignalsAfter", rendererReadyCount}, {"loadsBefore", loads}, {"loadsAfter", loadedCount}});
        check("M0.2 floating controls do not resize viewport", stableViewport);
        check("M0.2 pause survives fullscreen", property("pause").toBool());
        shot("viewport-window-zoom.png");
        require("M0.3 zoomed source screenshot", command({"screenshot-to-file", output + "/source-zoom.png", "video"}));
        check("M0.3 source unaffected by Qt zoom", QImage(output + "/source-zoom.png") == source);
        video->resetView(); QTest::qWait(100);

        const auto tracks = property("track-list").toArray();
        for (const auto &entry : tracks) {
            const auto track = entry.toObject();
            if (track["type"] != "sub") continue;
            const auto label = track["codec"].toString().contains("ass") ? QString("ass") : QString("srt");
            require("M0.4 select embedded " + label, command({"set", "sid", QString::number(track["id"].toInt())}));
            command({"set", "sub-visibility", "yes"});
            video->resetView(); QTest::qWait(200); shot(label + "-fit.png");
            zoom(5); shot(label + "-zoom.png");
            command({"set", "sub-visibility", "no"}); QTest::qWait(200); shot(label + "-zoom-off.png");
            command({"set", "sub-visibility", "yes"}); QTest::qWait(100);
            const QPoint start(video->width() / 2, video->height() / 2), end(start.x(), 40);
            QTest::mousePress(video, Qt::LeftButton, Qt::NoModifier, start);
            QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(video->mapToGlobal(end)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(video, &move);
            QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, end);
            QTest::qWait(100); shot(label + "-zoom-pan.png");
            command({"set", "sub-visibility", "no"}); QTest::qWait(100); shot(label + "-zoom-pan-off.png");
            video->resetView(); QTest::qWait(150); shot(label + "-fit-off.png");
        }
        command({"set", "sub-visibility", "yes"});
        for (const auto &extension : {QString("srt"), QString("ass"), QString("vtt")}) {
            const int before = property("track-list").toArray().size();
            require("M0.5 sub-add " + extension, command({"sub-add", fixtures + "/external." + extension, "select"}));
            check("M0.5 external track appears " + extension, waitFor([&] { return property("track-list").toArray().size() == before + 1; }));
            QTest::qWait(150); shot("external-" + extension + ".png");
        }
        require("M0.5 select second audio", command({"set", "aid", "2"}));
        check("M0.5 aid feedback", property("aid").toInt() == 2);
        require("M0.5 mute on", command({"set", "mute", "yes"}));
        check("M0.5 mute feedback", property("mute").toBool());
        command({"set", "mute", "no"});
        require("M0.5 speed", command({"set", "speed", "1.5"}));
        check("M0.5 speed feedback", property("speed").toDouble() == 1.5);
        check("M0.5 pitch correction", command({"set", "audio-pitch-correction", "yes"}) && property("audio-pitch-correction").toBool());
        const double beforeStep = property("time-pos").toDouble();
        require("M0.5 frame-step", command({"frame-step"}));
        const bool advanced = waitFor([&] { return property("time-pos").toDouble() > beforeStep + .01; });
        const double afterStep = property("time-pos").toDouble();
        check("M0.5 frame-step advances paused frame", advanced && property("pause").toBool() && std::abs(afterStep - beforeStep - .04) < .005,
              {{"before", beforeStep}, {"after", afterStep}});
        command({"set", "speed", "1"});
        command({"set", "ab-loop-a", "2"}); command({"set", "ab-loop-b", "3"});
        command({"seek", "2", "absolute+exact"}); command({"set", "pause", "no"});
        QJsonArray samples;
        double previous = -1; int wraps = 0;
        for (int i = 0; i < 35; ++i) {
            QTest::qWait(100);
            const double position = property("time-pos").toDouble();
            samples.append(position);
            if (previous > position + .5) ++wraps;
            previous = position;
        }
        command({"set", "pause", "yes"});
        check("M0.5 A-B wraps", wraps >= 2, {{"wraps", wraps}, {"samples", samples}});
        command({"set", "ab-loop-a", "no"}); command({"set", "ab-loop-b", "no"});
        require("M0.5 stop", command({"stop", "keep-playlist"}));
        check("M0.5 stop idle", waitFor([&] { return property("idle-active").toBool(); }));
        open(QString::fromUtf8("网格 multi.mkv")); pauseAt(2.4);
        require("M0.5 recover after stop", state == Player::State::Loaded);
    }

    void recoveryCases()
    {
        command({"set", "aid", "2"});
        command({"sub-add", fixtures + "/external.ass", "select"});
        command({"set", "speed", "1.5"}); command({"set", "sub-scale", "1.2"});
        command({"set", "sub-delay", "0.2"}); command({"set", "audio-delay", "0.1"});
        command({"set", "ab-loop-a", "2"}); command({"set", "ab-loop-b", "4"});
        command({"set", "sub-visibility", "yes"}); QTest::qWait(200);
        const auto before = snapshot();
        details["beforeContextRebuild"] = before;
        const int loadsBefore = loadedCount;
        player.setRendererReady(false);
        delete video; surface->video = nullptr;
        makeVideo();
        require("M0.6 forced context reload", waitFor([&] { return loadedCount > loadsBefore && state == Player::State::Loaded; }));
        QTest::qWait(250);
        details["afterContextRebuildRaw"] = snapshot();
        check("M0.6 current Player restores pause and position", property("pause").toBool() && std::abs(property("time-pos").toDouble() - before["time-pos"].toDouble()) < .1);
        // M4 production Player now performs the full documented restore sequence.
        const auto restored = snapshot(); details["afterContextRebuildRestored"] = restored;
        bool same = true;
        for (const auto *key : {"pause", "speed", "aid", "sub-scale", "sub-delay", "audio-delay", "ab-loop-a", "ab-loop-b", "sub-visibility"}) same &= before[key] == restored[key];
        bool selectedExternal = false;
        for (const auto &entry : restored["track-list"].toArray()) {
            const auto track = entry.toObject();
            selectedExternal |= track["type"] == "sub" && track["selected"].toBool()
                && QDir::fromNativeSeparators(track["external-filename"].toString()) == fixtures + "/external.ass";
        }
        check("M0.6 production restores full snapshot", same && selectedExternal && std::abs(before["time-pos"].toDouble() - restored["time-pos"].toDouble()) < .1);
        shot("context-restored.png");
        command({"set", "ab-loop-a", "no"}); command({"set", "ab-loop-b", "no"});
        const auto old = submit({"screenshot-to-file", output + "/old-generation.png", "video"});
        ++generation; player.openFile(fixtures + "/grid.mp4");
        ++generation; player.openFile(fixtures + QString::fromUtf8("/换片 C.mkv"));
        require("M0.6 newest file wins", waitFor([&] { return state == Player::State::Loaded && path.endsWith(QString::fromUtf8("换片 C.mkv")); }));
        waitFor([&] { return pending.at(old).done; });
        check("M0.6 stale reply recognized by generation", pending.at(old).done && pending.at(old).generation != generation);
        pauseAt(1.2);
        details["finalPath"] = path;
        details["finalGeneration"] = generation;
        details["finalPlaylist"] = property("playlist");
        check("M0.6 single mpv playlist entry", property("playlist").toArray().size() == 1);
    }

    void fixtureCases()
    {
        command({"set", "speed", "1"});
        open("portrait.mp4"); pauseAt(1.2);
        details["portraitOutput"] = property("video-out-params");
        shot("portrait-fit.png");
        const auto longFiles = QDir(fixtures).entryList({"long-*.mkv"}, QDir::Files);
        require("B03 long filename fixture exists", longFiles.size() == 1);
        open(longFiles.front()); pauseAt(1.2);
        check("B03 long Chinese space path loads", state == Player::State::Loaded, {{"pathLength", path.size()}});
        ++generation; player.openFile(fixtures + "/audio.m4a");
        require("B03 audio-only loads", waitFor([&] { return state == Player::State::Loaded && path.endsWith("audio.m4a"); }));
        QTest::qWait(100);
        bool audioOnly = true;
        for (const auto &track : property("track-list").toArray()) audioOnly &= track.toObject()["type"] != "video";
        check("B03 audio-only has no video track", audioOnly);
        ++generation; player.openFile(fixtures + "/broken.mkv");
        require("B03 corrupt file reports error", waitFor([&] { return state == Player::State::Error; }));
        open(QString::fromUtf8("网格 multi.mkv")); pauseAt(1.2);
        check("B03 recovers from corrupt file", state == Player::State::Loaded);
    }

    bool save()
    {
        drain();
        bool passed = true;
        for (const auto &entry : checks) passed &= entry.toObject()["passed"].toBool();
        QJsonObject report{{"passed", passed}, {"details", details}, {"checks", checks}, {"replies", replies}, {"events", events}};
        QFile file(output + "/results.json");
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(report).toJson());
        return passed;
    }
};
} // namespace

int main(int argc, char **argv)
{
    QElapsedTimer startup;
    startup.start();
    QApplication app(argc, argv);
    app.setApplicationName("zopana-m0");
    app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 4) { std::fprintf(stderr, "Usage: zopana_m0 baseline|baseline-perf|production-perf|probe FIXTURES OUTPUT\n"); return 2; }
    const auto mode = app.arguments()[1];
    try {
        const bool production = mode == "production-perf";
        Probe probe(app.arguments()[2], app.arguments()[3], production || mode.startsWith("baseline"), production);
        if (production) probe.details["startupToRendererMs"] = double(startup.elapsed());
        try {
            if (mode == "info") { /* Environment-only run for effective DPI calibration. */ }
            else if (production || mode.startsWith("baseline")) probe.baseline(production || mode == "baseline-perf");
            else { probe.renderCases(); probe.recoveryCases(); probe.fixtureCases(); }
        } catch (const std::exception &error) {
            probe.check("aborted", false, {{"reason", error.what()}});
        }
        return probe.save() ? 0 : 1;
    } catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
}
