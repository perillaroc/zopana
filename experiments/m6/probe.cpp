#include "mainwindow.h"
#include "playeractions.h"
#include "playlisthost.h"
#include "playlistcontroller.h"
#include "playlistpanel.h"
#include "videowidget.h"
#include "historystore.h"
#include "sessioncontroller.h"
#include "settingsstore.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QShortcutEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <windows.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <psapi.h>
#include <wrl/client.h>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace {
bool waitFor(const std::function<bool()> &condition, int timeout = 8000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

QJsonObject resources()
{
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    return {{"workingSetMiB", double(memory.WorkingSetSize) / 1048576}, {"privateMiB", double(memory.PrivateUsage) / 1048576},
            {"handles", int(handles)}, {"gdi", int(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS))},
            {"user", int(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS))}};
}

double cpuMilliseconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return double(ticks(kernel) + ticks(user)) / 10000;
}

class AudioMeter
{
public:
    bool find()
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return false;
        ComPtr<IMMDeviceCollection> devices;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return false;
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            ComPtr<IAudioSessionManager2> manager;
            if (FAILED(devices->Item(i, &device)) || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager))) continue;
            ComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED(manager->GetSessionEnumerator(&sessions))) continue;
            int size = 0;
            sessions->GetCount(&size);
            for (int j = 0; j < size; ++j) {
                ComPtr<IAudioSessionControl> control;
                ComPtr<IAudioSessionControl2> identity;
                DWORD pid = 0;
                if (FAILED(sessions->GetSession(j, &control)) || FAILED(control.As(&identity))) continue;
                if (SUCCEEDED(identity->GetProcessId(&pid)) && pid == GetCurrentProcessId() && SUCCEEDED(control.As(&meter_))) {
                    session_ = control;
                    return true;
                }
            }
        }
        return false;
    }

    QJsonObject sample() const
    {
        AudioSessionState state = AudioSessionStateInactive;
        float peak = 0;
        const bool valid = session_ && SUCCEEDED(session_->GetState(&state)) && SUCCEEDED(meter_->GetPeakValue(&peak));
        return {{"active", valid && state == AudioSessionStateActive}, {"peak", peak}};
    }
private:
    Microsoft::WRL::ComPtr<IAudioSessionControl> session_;
    Microsoft::WRL::ComPtr<IAudioMeterInformation> meter_;
};

class ShortcutEvents final : public QObject
{
public:
    QList<QString> matches;
    int ambiguous = 0;

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() != QEvent::Shortcut) return false;
        auto *shortcut = static_cast<QShortcutEvent *>(event);
        if (shortcut->isAmbiguous()) ++ambiguous;
        matches.append(object->objectName());
        return true;
    }
};

class CompositionEvents final : public QObject
{
public:
    QString preedit, commit;

    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::InputMethod) {
            const auto *input = static_cast<QInputMethodEvent *>(event);
            preedit += input->preeditString();
            commit += input->commitString();
        }
        return false;
    }
};

class Probe
{
public:
    explicit Probe(QString fixtures, QString output)
        : fixtures_(std::move(fixtures)), output_(std::move(output)), settings_(output_ + "/profile/config"),
          history_(output_ + "/profile/data"), window_(initialize()), session_(player_, window_, settings_, history_)
    {
        video_ = window_.findChild<VideoWidget *>();
        host_ = window_.findChild<PlaylistHost *>();
        actions_ = window_.findChild<PlayerActions *>();
        observer_ = mpv_create_client(player_.handle(), "m6-observer");
        if (!observer_) throw std::runtime_error("Observer initialization failed");
        window_.resize(1120, 720);
        window_.show();
        activate(window_, video_);
        require("renderer ready", waitFor([&] { return video_->rendererReady(); }));
    }

    ~Probe()
    {
        window_.close();
        player_.shutdown();
        mpv_destroy(observer_);
    }

    void shortcuts()
    {
        ViewGeometry geometry;
        geometry.setSizes({800, 500}, {1920, 1080});
        const QPointF anchor(300, 260);
        const QPointF original = (anchor - geometry.topLeft()) / geometry.frameSize().width();
        geometry.zoomAt(anchor, 2);
        const QPointF enlarged = (anchor - geometry.topLeft()) / geometry.frameSize().width();
        check("off-center anchor remains fixed through zoom", (original - enlarged).manhattanLength() < .000001);
        geometry.moveTo({-100000, -100000});
        check("pan clamps to both frame edges", geometry.topLeft() == QPointF(-800, -400));
        geometry.zoomAt(anchor, 100);
        check("zoom is capped at 8x", geometry.scale() == 8);
        geometry.setSizes({800, 500}, {720, 1280});
        geometry.reset();
        check("rotated display fits centered with letterbox", geometry.scale() == 1 && geometry.frameSize() == QSizeF(281.25, 500)
              && geometry.topLeft() == QPointF(259.375, 0));

        const auto *open = actions_->get(PlayerActions::Open);
        const auto *play = actions_->get(PlayerActions::PlayPause);
        const auto openIcon = open->icon().cacheKey();
        const auto playIcon = play->icon().cacheKey();
        actions_->setMediaState(true, false, false, true, false);
        check("media state replaces the play icon without replacing the open icon",
              !open->icon().isNull() && open->icon().cacheKey() == openIcon && play->icon().cacheKey() != playIcon);
        const auto pauseIcon = play->icon().cacheKey();
        actions_->refreshIcons();
        check("unchanged actions keep their rendered icons", play->icon().cacheKey() == pauseIcon);
        const auto originalPalette = window_.palette();
        auto alternatePalette = originalPalette;
        alternatePalette.setColor(QPalette::ButtonText, Qt::green);
        window_.setPalette(alternatePalette);
        actions_->refreshIcons();
        check("palette change invalidates the action icon", open->icon().cacheKey() != openIcon);
        window_.setPalette(originalPalette);
        actions_->setMediaState(false, true, false, false, false);

        const auto mainHwnd = reinterpret_cast<HWND>(window_.winId());
        const bool foreground = waitFor([&] { return GetForegroundWindow() == mainHwnd; }, 1500);

        ShortcutEvents events;
        for (int i = 0; i < PlayerActions::Count; ++i) {
            auto *action = actions_->get(PlayerActions::Id(i));
            action->installEventFilter(&events);
            action->setEnabled(true);
        }
        for (int i = 0; i < PlayerActions::Count; ++i) {
            auto *action = actions_->get(PlayerActions::Id(i));
            const auto scope = action->property("scope").toString();
            if (scope == QStringLiteral("队列") || scope == QStringLiteral("独立列表")) continue;
            for (const auto &sequence : action->shortcuts()) {
                const auto key = sequence[0];
                events.matches.clear();
                QTest::keyClick(video_, key.key(), key.keyboardModifiers());
                check("shortcut " + action->objectName() + " / " + sequence.toString(QKeySequence::PortableText),
                      events.matches == QList<QString>{action->objectName()});
            }
        }

        events.matches.clear();
        const bool nativeReady = foreground && GetForegroundWindow() == mainHwnd;
        details_["nativeInputAvailable"] = nativeReady;
        const auto native = [&](WORD scan, bool extended = false) {
            INPUT keys[2]{};
            for (auto &input : keys) {
                input.type = INPUT_KEYBOARD;
                input.ki.wScan = scan;
                input.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
            }
            keys[1].ki.dwFlags |= KEYEVENTF_KEYUP;
            if (GetForegroundWindow() != mainHwnd) return false;
            return SendInput(2, keys, sizeof(INPUT)) == 2;
        };
        if (nativeReady) {
            require("inject Win32 keypad plus", native(0x4e));
            check("Win32 keypad plus routes once", waitFor([&] { return !events.matches.isEmpty(); }, 500) && events.matches == QList<QString>{"view.zoomIn"});
            events.matches.clear();
            require("inject Win32 keypad minus", native(0x4a));
            check("Win32 keypad minus routes once", waitFor([&] { return !events.matches.isEmpty(); }, 500) && events.matches == QList<QString>{"view.zoomOut"});
            events.matches.clear();
            require("inject Win32 keypad Enter", native(0x1c, true));
            check("Win32 keypad Enter routes once", waitFor([&] { return !events.matches.isEmpty(); }, 500) && events.matches == QList<QString>{"window.fullscreen"});
        } else {
            std::puts("SKIP native input because the test window is not the Win32 foreground");
        }

        events.matches.clear();
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "), true);
        QApplication::sendEvent(video_, &space);
        check("space autorepeat is suppressed", events.matches.isEmpty());
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, {}, true);
        QApplication::sendEvent(video_, &right);
        check("seek autorepeat dispatches once", events.matches == QList<QString>{"seek.forwardSmall"});
        check("no ambiguous shortcut event", events.ambiguous == 0);

        QLineEdit edit(&window_);
        edit.setGeometry(30, 80, 280, 36);
        CompositionEvents composition;
        edit.installEventFilter(&composition);
        edit.show();
        edit.raise();
        edit.setFocus();
        events.matches.clear();
        if (nativeReady) {
            for (WORD scan : {0x31, 0x17, 0x23, 0x1e, 0x18, 0x39}) {
                require("inject native editor key", native(scan));
                QTest::qWait(65);
            }
        } else {
            QTest::keyClicks(&edit, "nihao");
        }
        QTest::qWait(120);
        check("editor input leaves watching shortcuts inactive", events.matches.isEmpty());
        details_["nativeIme"] = QJsonObject{{"observedPreedit", !composition.preedit.isEmpty()},
            {"observedCommit", !composition.commit.isEmpty()}, {"keyboardLanguage", int(LOWORD(reinterpret_cast<quintptr>(GetKeyboardLayout(0))))}};
        edit.hide();

        bool licenseShown = false;
        const auto inspectLicense = [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            const auto *text = dialog->findChild<QPlainTextEdit *>();
            QFile license(":/licenses/lucide.txt");
            licenseShown = text && license.open(QIODevice::ReadOnly) && text->toPlainText().trimmed() == QString::fromUtf8(license.readAll()).trimmed();
            dialog->accept();
        };
        QTimer::singleShot(100, &window_, inspectLicense);
        actions_->get(PlayerActions::Licenses)->trigger();
        check("Help opens the complete bundled license", licenseShown);
        licenseShown = false;
        QTimer::singleShot(100, &window_, [&] {
            auto *about = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!about) return;
            if (auto *button = about->findChild<QPushButton *>("aboutLicenses")) {
                QTimer::singleShot(100, &window_, inspectLicense);
                QTest::mouseClick(button, Qt::LeftButton);
            } else {
                about->reject();
            }
        });
        actions_->get(PlayerActions::About)->trigger();
        check("About opens the complete bundled license", licenseShown);

        auto *queue = window_.findChild<PlaylistPanel *>()->view();
        host_->setVisible(true, false);
        host_->setDetached(true);
        auto *list = host_->window();
        activate(*list, queue);
        auto *closePlaylist = actions_->get(PlayerActions::ClosePlaylist);
        closePlaylist->removeEventFilter(&events);
        QSignalSpy closed(closePlaylist, &QAction::triggered);
        QTest::keyClick(queue, Qt::Key_W, Qt::ControlModifier);
        check("detached playlist shortcut hides only the list and routes once",
              closed.size() == 1 && !host_->visible() && !list->isVisible() && window_.isVisible());
        host_->setVisible(true, false);
        activate(*list, queue);
        for (auto id : {PlayerActions::QueuePlay, PlayerActions::QueueRemove, PlayerActions::QueueUp, PlayerActions::QueueDown}) {
            auto *action = actions_->get(id);
            QSignalSpy triggered(action, &QAction::triggered);
            for (const auto &sequence : action->shortcuts()) {
                action->setEnabled(true);
                triggered.clear();
                const auto key = sequence[0];
                QTest::keyClick(queue, key.key(), key.keyboardModifiers());
                check("queue shortcut " + action->objectName() + " / " + sequence.toString(QKeySequence::PortableText),
                      triggered.size() == 1);
            }
        }
        check("no ambiguous shortcut in either window", events.ambiguous == 0);
    }

    void stress()
    {
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto uninitialize = qScopeGuard([com] { if (SUCCEEDED(com)) CoUninitialize(); });
        player_.setVolume(12);
        auto *queue = window_.findChild<PlaylistController *>();
        queue->addFiles({fixtures_ + "/motion-1080.mp4", fixtures_ + "/grid.mp4"}, true);
        require("media and render frame ready", waitFor([&] { return player_.state() == Player::State::Loaded && video_->canCapture() && player_.lastPosition() > 0; }));
        player_.setPaused(true);
        require("pause confirmed", waitFor([&] { return player_.paused(); }));
        QTest::qWait(100);
        const auto pausedFrame = video_->grabFramebuffer();
        const auto generation = player_.generation();
        auto *context = video_->context();
        auto *list = host_->window();
        const auto startPosition = player_.position();
        const auto cycle = [&] {
            host_->setVisible(true, false);
            host_->setDetached(true);
            activate(*list, list);
            QTest::keyClick(list, Qt::Key_F11);
            const bool full = window_.isFullScreen();
            host_->setVisible(false, false);
            host_->setDetached(false);
            activate(window_, video_);
            QTest::keyClick(video_, Qt::Key_F11);
            return full && !window_.isFullScreen() && player_.generation() == generation && video_->context() == context;
        };
        for (int i = 0; i < 4; ++i) require("window cycle warm-up", cycle());
        QJsonArray samples{resources()};
        bool stable = true;
        for (int i = 0; i < 20; ++i) {
            stable &= cycle();
            samples.append(resources());
        }
        check("20 mixed cycles preserve context, generation and pause", stable && player_.paused() && std::abs(player_.position() - startPosition) < .06);
        details_["cycleResources"] = samples;
        QTest::qWait(100);
        check("paused frame repaints after mixed cycles", !pausedFrame.isNull() && video_->grabFramebuffer() == pausedFrame);

        player_.setPaused(false);
        AudioMeter meter;
        require("WASAPI session found for player PID", waitFor([&] { return meter.find(); }, 3000));
        const auto idle = audioInterval(meter, {});
        int gesturesPerformed = 0, windowChanges = 0;
        const auto gestures = audioInterval(meter, [&] {
            const auto point = video_->rect().center();
            const int sign = gesturesPerformed++ % 4 < 2 ? 1 : -1;
            QWheelEvent wheel(point, video_->mapToGlobal(point), {}, {0, sign * 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(video_, &wheel);
            QTest::mousePress(video_, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(video_, point + QPoint(sign * 36, -20));
            QTest::mouseRelease(video_, Qt::LeftButton, Qt::NoModifier, point + QPoint(sign * 36, -20));
        });
        details_["audioBaseline"] = idle;
        details_["audioGestures"] = gestures;
        check("ten-second zoom and drag keep clock and WASAPI audio active", gestures["advanced"].toDouble() >= 9.5
              && gestures["quietMs"].toDouble() <= 60 && idle["quietMs"].toDouble() <= 60 && gestures["mediaCommands"].toInt() == 0
              && gesturesPerformed > 20);
        const auto reconfigured = audioInterval(meter, [&] {
            switch (windowChanges++ % 3) {
            case 0: actions_->get(PlayerActions::RotateNext)->trigger(); break;
            case 1: actions_->get(PlayerActions::Fullscreen)->trigger(); break;
            case 2:
                host_->setVisible(true, false);
                host_->setDetached(windowChanges % 2 == 0);
                actions_->get(PlayerActions::Scale50)->trigger();
                break;
            }
        });
        details_["audioWindowChanges"] = reconfigured;
        check("rotation and window changes keep clock and WASAPI audio active without a reload",
              reconfigured["advanced"].toDouble() >= 9.5 && reconfigured["quietMs"].toDouble() <= 60
              && reconfigured["mediaCommands"].toInt() == 0 && windowChanges > 20);

        host_->setVisible(false, false);
        host_->setDetached(false);
        window_.showNormal();
        window_.setWindowSizing(WindowSizePolicy::LastGeometry, 1);
        actions_->get(PlayerActions::RotateReset)->trigger();
        activate(window_, video_);
        player_.openFile(QDir(fixtures_).absoluteFilePath("../zopana-ui-m2-v1/long-grid.mp4"));
        require("long audio fixture playing", waitFor([&] { return player_.state() == Player::State::Loaded && player_.position() > .2 && !player_.paused(); }));
        require("long fixture has active audio", waitFor([&] { return meter.find() && meter.sample()["peak"].toDouble() > .00002; }, 2500));
        window_.showFullScreen();
        activate(window_, video_);
        const auto savedPointer = QCursor::pos();
        const auto restorePointer = qScopeGuard([=] { QCursor::setPos(savedPointer); });
        auto *bar = window_.findChild<QWidget *>("controlBar");
        auto *seek = bar->findChild<QWidget *>("seekSlider");
        require("fullscreen seek bar is available", seek != nullptr);
        const auto edge = QPoint(video_->width() / 2, video_->height() - 2);
        QTest::mouseMove(video_, edge);
        require("bottom edge reveals the fullscreen controls", waitFor([&] { return bar->isVisible(); }));
        seek->setFocus();
        details_["controlsVisible"] = samplePhase(*bar, 4500);
        video_->setFocus();
        QCursor::setPos(video_->mapToGlobal(video_->rect().center()));
        require("fullscreen controls auto-hide", waitFor([&] { return bar->isHidden(); }, 5000));
        details_["controlsHidden"] = samplePhase(*bar, 4500);
        check("visible and hidden fullscreen phases measured", details_["controlsVisible"].toObject()["visibleSamples"].toInt() > 10
              && details_["controlsHidden"].toObject()["visibleSamples"].toInt() == 0);
        window_.showNormal();
        window_.showMinimized();
        const double before = player_.position();
        const auto background = samplePhase(*bar, 3000);
        details_["minimized"] = background;
        check("minimized playback clock and audio continue", window_.isMinimized() && player_.position() - before >= 2.5
              && meter.sample()["peak"].toDouble() > .00002);
        window_.showNormal();
        require("video returns after minimization", waitFor([&] { return video_->canCapture(); }));
    }

    void check(const QString &name, bool passed)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", passed}});
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData());
        std::fflush(stdout);
    }

    bool save()
    {
        bool passed = true;
        for (const auto &row : checks_) passed &= row.toObject()["passed"].toBool();
        details_["effectiveDpr"] = video_->devicePixelRatioF();
        details_["screens"] = QApplication::screens().size();
        QDir().mkpath(output_);
        QFile file(output_ + "/results.json");
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(QJsonObject{{"passed", passed}, {"checks", checks_}, {"details", details_}}).toJson()) > 0 && passed;
    }

private:
    Player &initialize()
    {
        if (!player_.initialize()) throw std::runtime_error("Playback core initialization failed");
        return player_;
    }

    void require(const QString &name, bool passed)
    {
        check(name, passed);
        if (!passed) throw std::runtime_error(name.toStdString());
    }

    void activate(QWidget &window, QWidget *focus)
    {
        window.show();
        window.raise();
        window.activateWindow();
        if (!waitFor([&] { return QApplication::activeWindow() == &window; }, 1500))
            throw std::runtime_error("Acceptance window did not activate");
        focus->setFocus();
    }

    double number(const char *property)
    {
        double value = -1;
        mpv_get_property(observer_, property, MPV_FORMAT_DOUBLE, &value);
        return value;
    }

    QJsonObject audioInterval(AudioMeter &meter, const std::function<void()> &interact)
    {
        player_.setPaused(true);
        require("audio measurement pause", waitFor([&] { return player_.paused(); }));
        player_.seek(1);
        require("audio measurement starts at same frame", waitFor([&] { return std::abs(player_.position() - 1) < .06; }));
        video_->resetView();
        video_->zoomSteps(4);
        QTest::qWait(200);
        QSignalSpy requests(&player_, &Player::requestSubmitted);
        QElapsedTimer elapsed;
        QJsonArray samples;
        double quietSince = -1, longestQuiet = 0;
        QTimer input;
        if (interact) QObject::connect(&input, &QTimer::timeout, video_, interact);
        player_.setPaused(false);
        require("audio playback starts", waitFor([&] { return !player_.paused() && meter.sample()["peak"].toDouble() > .00002; }));
        if (interact) input.start(300);
        const double start = player_.position();
        elapsed.start();
        while (elapsed.elapsed() < 10500) {
            QTest::qWait(20);
            auto sample = meter.sample();
            const double now = elapsed.elapsed();
            sample["ms"] = now;
            sample["position"] = player_.position();
            samples.append(sample);
            if (!sample["active"].toBool() || sample["peak"].toDouble() < .00002) {
                if (quietSince < 0) quietSince = now;
                longestQuiet = std::max(longestQuiet, now - quietSince + 20);
            } else quietSince = -1;
        }
        input.stop();
        int commands = 0;
        for (const auto &request : requests) {
            const auto op = request.value(3).toString();
            if (op.startsWith("seek ") || op.startsWith("loadfile ")) ++commands;
        }
        return {{"advanced", player_.position() - start}, {"quietMs", longestQuiet}, {"mediaCommands", commands}, {"samples", samples}};
    }

    QJsonObject samplePhase(QWidget &bar, int milliseconds)
    {
        QElapsedTimer elapsed;
        const double cpu = cpuMilliseconds();
        const double drops = number("frame-drop-count"), decoderDrops = number("decoder-frame-drop-count");
        QJsonArray memory;
        int visible = 0;
        elapsed.start();
        while (elapsed.elapsed() < milliseconds) {
            QTest::qWait(250);
            visible += bar.isVisible();
            memory.append(resources());
        }
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        return {{"cpuPercentMachine", 100 * (cpuMilliseconds() - cpu) / elapsed.elapsed() / system.dwNumberOfProcessors},
                {"frameDrop", number("frame-drop-count") - drops}, {"decoderDrop", number("decoder-frame-drop-count") - decoderDrops},
                {"visibleSamples", visible}, {"memory", memory}};
    }

    QString fixtures_, output_;
    SettingsStore settings_;
    HistoryStore history_;
    Player player_;
    MainWindow window_;
    SessionController session_;
    VideoWidget *video_ = nullptr;
    PlaylistHost *host_ = nullptr;
    PlayerActions *actions_ = nullptr;
    mpv_handle *observer_ = nullptr;
    QJsonArray checks_;
    QJsonObject details_;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    if (app.arguments().size() != 4) return 2;
    try {
        Probe probe(app.arguments()[2], app.arguments()[3]);
        try {
            if (app.arguments()[1] == "shortcuts") probe.shortcuts();
            else if (app.arguments()[1] == "stress") probe.stress();
            else return 2;
        } catch (const std::exception &error) { probe.check(QString::fromUtf8(error.what()), false); }
        return probe.save() ? 0 : 1;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
