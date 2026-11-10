#include "mainwindow.h"
#include "playeractions.h"
#include "playbackdialogs.h"
#include "videowidget.h"
#include "playlistcontroller.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QSlider>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
bool until(const std::function<bool()> &condition, int timeout = 6000)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

class Suite
{
public:
    Player player;
    MainWindow window;
    VideoWidget *video;
    QString fixtures, output, path;
    Player::State state = Player::State::Idle;
    mpv_handle *observer = nullptr;
    QJsonArray checks;
    int loads = 0;
    Suite(QString media, QString destination) : window(initialize()), fixtures(QDir::fromNativeSeparators(media)), output(destination)
    {
        QDir().mkpath(output);
        observer = mpv_create_client(player.handle(), "m2-test-observer");
        video = widget<VideoWidget>("videoWidget");
        QObject::connect(&player, &Player::mediaChanged, &window, [this](auto value, const QString &file, const QString &) {
            state = value; path = file; if (value == Player::State::Loaded) ++loads;
        });
        window.show(); window.raise(); window.activateWindow();
        require("renderer ready", until([this] { return video->rendererReady(); }));
        require("window active", QTest::qWaitForWindowActive(&window)); video->setFocus();
    }
    Player &initialize() { if (!player.initialize()) throw std::runtime_error("initialization failed"); return player; }
    ~Suite() { player.shutdown(); if (observer) mpv_destroy(observer); }
    template<class T> T *widget(const QString &name) { return window.findChild<T *>(name); }
    QAction *action(const char *name) { return widget<QAction>(name); }
    void check(const QString &name, bool passed, QJsonObject fields = {})
    {
        fields["name"] = name; fields["passed"] = passed; checks.append(fields);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData()); std::fflush(stdout);
    }
    void require(const QString &name, bool passed) { check(name, passed); if (!passed) throw std::runtime_error(name.toStdString()); }
    double number(const char *name)
    {
        double value = -100; mpv_get_property(observer, name, MPV_FORMAT_DOUBLE, &value); return value;
    }
    bool flag(const char *name) { int value = 0; mpv_get_property(observer, name, MPV_FORMAT_FLAG, &value); return value; }
    void command(const QStringList &arguments)
    {
        std::vector<QByteArray> bytes; for (const auto &argument : arguments) bytes.push_back(argument.toUtf8());
        std::vector<const char *> args; for (const auto &argument : bytes) args.push_back(argument.constData()); args.push_back(nullptr);
        if (mpv_command(observer, args.data()) < 0) throw std::runtime_error("test command failed");
    }
    int seekEvents()
    {
        int count = 0;
        for (;;) { auto *event = mpv_wait_event(observer, 0); if (event->event_id == MPV_EVENT_NONE) return count; if (event->event_id == MPV_EVENT_SEEK) ++count; }
    }
    void open(const QString &name = QString::fromUtf8("网格 multi.mkv"))
    {
        const int before = loads; player.openFile(fixtures + '/' + name);
        require("load " + name, until([&] { return loads > before && state == Player::State::Loaded; }));
        require("seek ready", until([this] { return action("seek.goto")->isEnabled() && player.lastPosition() > 0; }));
        player.setPaused(true); require("pause confirmed", until([this] { return flag("pause"); }));
        QTest::qWait(80); video->setFocus();
    }
    void seek(double target)
    {
        player.seek(target);
        require(QString("seek %1").arg(target), until([&] { return std::abs(number("time-pos") - target) < .08; })); QTest::qWait(70);
    }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::keyClick(video, key, modifiers); }
    void wheel(double steps, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const auto point = video->rect().center();
        QWheelEvent event(point, video->mapToGlobal(point), {}, QPoint(0, qRound(120 * steps)), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(video, &event);
    }
    void screenshot(const QString &name) { check("screenshot " + name, window.grab().save(output + '/' + name + ".png")); }
    void drop(const QStringList &files, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMimeData mime; QList<QUrl> urls;
        for (const auto &file : files) urls.append(file.contains("://") ? QUrl(file) : QUrl::fromLocalFile(file));
        mime.setUrls(urls);
        const auto position = video->mapTo(&window, video->rect().center());
        QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, modifiers); QApplication::sendEvent(&window, &enter);
        QDropEvent event(position, Qt::CopyAction, &mime, Qt::LeftButton, modifiers); QApplication::sendEvent(&window, &event);
    }
    void parser()
    {
        for (auto [text, seconds] : {std::pair{"1:02:03.125", 3723.125}, {"02:03.5", 123.5}, {"12.001", 12.001}, {"0", 0.0}})
            check(QString("time syntax %1").arg(text), PlaybackDialogs::parseTime(text) == std::optional(seconds));
        for (auto text : {"", "-1", "1:60", "1:00:60", "nan", "1e9", "1:2:3:4", "1.0001", "1.2:3", "abc"})
            check(QString("reject invalid time %1").arg(text), !PlaybackDialogs::parseTime(text));
    }
    void playback()
    {
        check("idle media actions disabled", !action("play.stop")->isEnabled() && !action("seek.goto")->isEnabled() && !action("audio.mute")->isEnabled());
        open();
        check("audio controls enabled", action("audio.mute")->isEnabled());
        key(Qt::Key_S); require("S stops", until([this] { return state == Player::State::Idle && flag("idle-active"); }));
        const int before = loads; QTest::qWait(300);
        check("stop retains path without automatic replay", path.endsWith(QString::fromUtf8("网格 multi.mkv")) && loads == before && action("play.toggle")->isEnabled());
        key(Qt::Key_Space); require("Space replays stopped file", until([&] { return loads > before && !flag("pause"); }));
        check("replay starts at beginning", number("time-pos") < 1);
        player.openFile(fixtures + "/motion-4k.mp4"); player.stop();
        require("stop while loading drains and stays stopped", until([this] { return state == Player::State::Idle && flag("idle-active"); }));
        open(); seek(1); seekEvents();
        QSignalSpy targets(&player, &Player::seekRequested);
        for (int i = 0; i < 100; ++i) player.seekRelative(.05);
        check("relative input accumulates before engine updates", std::abs(player.positionTarget() - 6) < .08 && targets.count() == 100);
        require("coalesced seek reaches accumulated target", until([this] { return std::abs(number("time-pos") - 6) < .08; })); QTest::qWait(120);
        const int submitted = seekEvents(); check("100 inputs submit bounded seeks", submitted <= 2 && submitted >= 1, {{"engineSeeks", submitted}});
        check("seek preserves paused state", flag("pause"));
        seek(0); key(Qt::Key_Right); require("Right seeks five seconds", until([this] { return std::abs(number("time-pos") - 5) < .08; })); QTest::qWait(70);
        key(Qt::Key_Left); require("Left returns to start", until([this] { return number("time-pos") < .08; })); QTest::qWait(70);
        for (auto modifiers : {Qt::ShiftModifier, Qt::ControlModifier}) {
            key(Qt::Key_Right, modifiers); require("large seek clamps to final frame", until([this] { return std::abs(number("time-pos") - player.lastPosition()) < .08; })); QTest::qWait(70);
            key(Qt::Key_Left, modifiers); require("large backward seek clamps at zero", until([this] { return number("time-pos") < .08; })); QTest::qWait(70);
        }
        key(Qt::Key_End); require("End reaches final video frame", until([this] { return std::abs(number("time-pos") - 15.96) < .08; }));
        check("End does not emit EOF while paused", state == Player::State::Loaded && flag("pause"));
        screenshot("last-frame"); QTest::qWait(70); key(Qt::Key_Home); require("Home reaches start", until([this] { return number("time-pos") < .08; }));
        seek(4); player.seekRelative(8); player.openFile(fixtures + "/grid.mp4"); player.openFile(fixtures + QString::fromUtf8("/换片 C.mkv"));
        require("rapid replacement selects newest file", until([this] { return state == Player::State::Loaded && path.endsWith(QString::fromUtf8("换片 C.mkv")); }));
        QTest::qWait(250); check("previous pending seek never applies to next file", number("time-pos") < 1);
        player.setPaused(true); until([this] { return flag("pause"); });
        open("../zopana-ui-m2-v1/long-grid.mp4");
        for (auto [modifiers, step] : {std::pair{Qt::NoModifier, 5}, {Qt::ShiftModifier, 30}, {Qt::ControlModifier, 60}}) {
            seek(70); key(Qt::Key_Right, modifiers);
            require(QString("forward shortcut moves %1 seconds").arg(step), until([&] { return std::abs(number("time-pos") - 70 - step) < .08; })); QTest::qWait(70);
            key(Qt::Key_Left, modifiers);
            require(QString("back shortcut moves %1 seconds").arg(step), until([&] { return std::abs(number("time-pos") - 70) < .08; })); QTest::qWait(70);
        }
        seek(70); seekEvents();
        key(Qt::Key_Right); QTest::qWait(40);
        for (int i = 0; i < 30; ++i) player.seekRelative(.1);
        require("new targets coalesce while prior seek is running", until([this] { return std::abs(number("time-pos") - 78) < .1; }));
        QTest::qWait(150); check("in-flight seek queue stays bounded", seekEvents() <= 3 && flag("pause"));
        open("audio.m4a"); key(Qt::Key_End); QTest::qWait(200);
        check("audio End remains loaded and paused", state == Player::State::Loaded && flag("pause") && number("time-pos") > 15.5,
            {{"position", number("time-pos")}, {"endTarget", player.lastPosition()}});
        open(); command({"set", "speed", "8"}); command({"seek", "15.7", "absolute+exact"}); player.setPaused(false);
        require("EOF distinct from user stop", until([this] { return state == Player::State::Ended; }));
        check("EOF presents replay", widget<QLabel>("cardTitle")->text() == QStringLiteral("播放完毕"));
        command({"set", "speed", "1"}); action("play.toggle")->trigger(); require("replay after EOF", until([this] { return state == Player::State::Loaded; }));
        player.setPaused(true); until([this] { return flag("pause"); });
    }
    void audioAndView()
    {
        player.setVolume(50); until([this] { return std::abs(number("volume") - 50) < .01; });
        key(Qt::Key_M); require("M mutes with engine feedback", until([this] { return flag("mute"); }));
        check("mute action reflects engine state", until([this] { return action("audio.mute")->isChecked() && widget<QLabel>("volumeLabel")->text() == QStringLiteral("静音"); }));
        key(Qt::Key_Up); require("volume key unmutes from saved volume", until([this] { return !flag("mute") && std::abs(number("volume") - 55) < .01; }));
        key(Qt::Key_Down); require("Down lowers five points", until([this] { return std::abs(number("volume") - 50) < .01; }));
        key(Qt::Key_M); until([this] { return flag("mute"); });
        widget<QSlider>("volumeSlider")->setValue(48); require("volume slider unmutes", until([this] { return !flag("mute") && std::abs(number("volume") - 48) < .01; }));
        video->setFocus(); key(Qt::Key_V);
        check("V switches to regular mode", action("view.mode.standard")->isChecked());
        for (int i = 0; i < 12; ++i) wheel(.1);
        require("fractional wheel volume accumulates", until([this] { return std::abs(number("volume") - 54) < .01; }));
        wheel(100); require("volume upper bound", until([this] { return std::abs(number("volume") - 100) < .01; }));
        wheel(-100); require("volume lower bound", until([this] { return std::abs(number("volume")) < .01; }));
        const auto fit = video->grabFramebuffer();
        wheel(2, Qt::ControlModifier); QTest::qWait(140);
        check("Ctrl wheel zooms without changing mode or volume", video->zoomScale() > 1.3 && action("view.mode.standard")->isChecked() && number("volume") == 0);
        key(Qt::Key_R); QTest::qWait(80); check("R restores original paused frame", video->grabFramebuffer() == fit && video->zoomScale() == 1);
        for (auto [keyValue, modifiers] : {std::pair{Qt::Key_Equal, Qt::NoModifier}, {Qt::Key_Plus, Qt::ShiftModifier}, {Qt::Key_Plus, Qt::KeypadModifier}}) {
            key(keyValue, modifiers); QTest::qWait(110);
            check("zoom keyboard alias increases scale", video->zoomScale() > 1.1); key(Qt::Key_R); QTest::qWait(30);
        }
        key(Qt::Key_Equal, Qt::ControlModifier); QTest::qWait(80); check("Ctrl equals does not trigger plain zoom", video->zoomScale() == 1);
        key(Qt::Key_Equal); QTest::qWait(100); key(Qt::Key_Minus, Qt::KeypadModifier); QTest::qWait(100);
        check("keypad minus zooms out", video->zoomScale() == 1);
        for (int i = 0; i < 40; ++i) key(Qt::Key_Equal);
        QTest::qWait(150); check("zoom clamps at eight", video->zoomScale() == 8 && widget<QLabel>("zoomLabel")->text() == QStringLiteral("8.00×"));
        key(Qt::Key_R); QTest::qWait(80); key(Qt::Key_V); check("V returns to zoom mode", action("view.mode.feature")->isChecked());
        key(Qt::Key_V); QTest::qWait(80);
    }
    void mouse()
    {
        QSignalSpy toggles(action("play.toggle"), &QAction::triggered);
        const auto point = video->rect().center();
        QTest::mouseClick(video, Qt::LeftButton, Qt::NoModifier, point);
        check("single click waits for double click interval", toggles.isEmpty());
        require("single click toggles once after interval", until([&] { return toggles.count() == 1; }, QApplication::doubleClickInterval() + 200));
        until([this] { return !flag("pause"); }); player.setPaused(true); until([this] { return flag("pause"); }); toggles.clear();
        QTest::mouseClick(video, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(video, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(QApplication::doubleClickInterval() + 80);
        check("double click enters fullscreen without playback toggle", window.isFullScreen() && toggles.isEmpty() && flag("pause"));
        key(Qt::Key_F11); QTest::qWait(80);
        QTest::mousePress(video, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(video, point + QPoint(QApplication::startDragDistance() + 20, 0)); QTest::mouseMove(video, point);
        QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(QApplication::doubleClickInterval() + 50);
        check("drag crossing threshold and returning never clicks", toggles.isEmpty());
        QTest::mouseClick(video, Qt::LeftButton, Qt::NoModifier, point); action("view.mode.feature")->trigger();
        QTest::qWait(QApplication::doubleClickInterval() + 50); check("mode change cancels pending click", toggles.isEmpty());
        action("view.mode.standard")->trigger();
        QTest::mouseClick(video, Qt::LeftButton, Qt::NoModifier, point);
        QFocusEvent lost(QEvent::FocusOut); QApplication::sendEvent(video, &lost); QTest::qWait(QApplication::doubleClickInterval() + 50);
        check("focus loss cancels pending click", toggles.isEmpty());
        QTest::mousePress(video, Qt::LeftButton, Qt::NoModifier, point); const auto scale = video->zoomScale(), volume = number("volume");
        wheel(2); wheel(2, Qt::ControlModifier); QTest::mouseMove(video, point + QPoint(50, 0)); QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, point + QPoint(50, 0));
        QTest::qWait(150); check("wheel is consumed during drag in regular mode", video->zoomScale() == scale && number("volume") == volume);
        action("view.mode.feature")->trigger(); QTest::mouseDClick(video, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(80);
        check("zoom mode double click toggles fullscreen only", window.isFullScreen() && toggles.isEmpty()); key(Qt::Key_F11); QTest::qWait(80);
    }
    void timelineAndDialogs()
    {
        auto *slider = widget<QSlider>("seekSlider"); seek(2);
        QSignalSpy requests(&player, &Player::seekRequested);
        const auto hover = slider->rect().center();
        QCursor::setPos(slider->mapToGlobal(hover)); QTest::qWait(30);
        QMouseEvent hoverEvent(QEvent::MouseMove, hover, slider->mapToGlobal(hover), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(slider, &hoverEvent); QTest::qWait(50);
        auto *hoverTime = widget<QLabel>("seekHoverTime");
        check("hover displays time without seeking", requests.isEmpty() && hoverTime->isVisible() && hoverTime->text().contains(':'),
            {{"seekRequests", requests.count()}, {"previewVisible", hoverTime->isVisible()}, {"previewText", hoverTime->text()}});
        const auto point = QPoint(slider->width() * 3 / 4, slider->height() / 2);
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, point); QTest::mouseMove(slider, point - QPoint(20, 0));
        check("drag only previews", slider->isSliderDown() && requests.isEmpty() && number("time-pos") < 2.1);
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, point);
        check("release commits one seek", requests.count() == 1); QTest::qWait(200);
        seek(2); requests.clear();
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, point); QTest::keyClick(slider, Qt::Key_Escape);
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, point); QTest::qWait(100);
        check("Escape cancels drag without seek", !slider->isSliderDown() && requests.isEmpty() && std::abs(number("time-pos") - 2) < .08);
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, point); QEvent ungrab(QEvent::UngrabMouse); QApplication::sendEvent(slider, &ungrab);
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, point); check("grab loss cancels drag", !slider->isSliderDown() && requests.isEmpty());
        slider->setFocus(); const auto before = number("time-pos"); QTest::keyClick(slider, Qt::Key_Right);
        require("focused slider retains one-second step", until([&] { return std::abs(number("time-pos") - before - 1) < .08; }));
        bool invalidShown = false, safe = false;
        QTimer::singleShot(80, &window, [&] {
            auto *dialog = QApplication::activeModalWidget(); if (!dialog) return;
            auto *edit = dialog->findChild<QLineEdit *>("gotoTimeEdit"); if (!edit) return;
            edit->setText("99:99"); QTest::keyClick(edit, Qt::Key_Return);
            invalidShown = dialog->isVisible() && !dialog->findChild<QLabel *>("gotoTimeError")->text().isEmpty();
            QTest::keyClick(edit, Qt::Key_F11); safe = !window.isFullScreen() && flag("pause");
            edit->setText("00:04.125"); QTest::keyClick(edit, Qt::Key_Return);
        });
        video->setFocus(); key(Qt::Key_G, Qt::ControlModifier);
        check("Ctrl G validates in place and protects modal input", invalidShown && safe);
        require("millisecond time dialog commits exact target", until([this] { return std::abs(number("time-pos") - 4.125) < .08; }));
        bool help = false;
        QTimer::singleShot(80, &window, [&] {
            auto *dialog = QApplication::activeModalWidget(); if (!dialog) return;
            auto *table = dialog->findChild<QTableWidget *>("shortcutsTable");
            help = table && table->rowCount() == PlayerActions::Count;
            auto *actions = window.findChild<PlayerActions *>();
            for (int row = 0; help && row < PlayerActions::Count; ++row) {
                auto *a = actions->get(PlayerActions::Id(row)); QStringList keys;
                for (const auto &key : a->shortcuts()) keys.append(key.toString(QKeySequence::NativeText));
                help &= table->item(row, 0)->text() == a->text() && (keys.isEmpty() || table->item(row, 1)->text() == keys.join(" / "));
            }
            screenshot("shortcuts"); QTest::keyClick(dialog, Qt::Key_Escape);
        });
        key(Qt::Key_F1); check("F1 help matches live action table", help);
        auto *edit = new QLineEdit(&window); edit->setGeometry(20, 70, 220, 32); edit->show(); edit->raise(); edit->setFocus();
        QSignalSpy play(action("play.toggle"), &QAction::triggered); requests.clear();
        QTest::keyClicks(edit, "svmr +=- "); QTest::keyClick(edit, Qt::Key_Right); QTest::keyClick(edit, Qt::Key_Up);
        check("editing protects all watching commands", state == Player::State::Loaded && flag("pause") && play.isEmpty() && requests.isEmpty() && !window.isFullScreen());
        delete edit; video->setFocus();
    }
    void fullscreen()
    {
        auto *bar = widget<QWidget>("controlBar"); auto *context = video->context(); const int before = loads;
        video->setFocus(); key(Qt::Key_F11); QTest::qWait(150);
        QCursor::setPos(video->mapToGlobal(QPoint(video->width() / 2, 180))); QTest::qWait(100);
        const auto geometry = video->geometry(); const auto frame = video->grabFramebuffer();
        const auto barImage = bar->grab().toImage();
        check("fullscreen bar has an opaque palette background", barImage.pixelColor(2, 2) == bar->palette().color(QPalette::Button));
        window.raise(); window.activateWindow(); video->setFocus();
        require("fullscreen test window active", QTest::qWaitForWindowActive(&window));
        const bool hidden = until([&] { return !bar->isVisible(); }, 3600);
        check("fullscreen auto hides controls after inactivity", hidden,
            {{"active", window.isActiveWindow()}, {"focus", QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : "none"},
             {"popup", QApplication::activePopupWidget() != nullptr}, {"modal", QApplication::activeModalWidget() != nullptr},
             {"pointerInBar", bar->rect().contains(bar->mapFromGlobal(QCursor::pos()))}, {"gesture", video->gestureActive()}});
        if (!hidden) throw std::runtime_error("fullscreen did not hide");
        check("hiding preserves viewport and paused frame", geometry == video->geometry() && frame == video->grabFramebuffer() && window.cursor().shape() == Qt::BlankCursor);
        screenshot("fullscreen-hidden"); key(Qt::Key_M); QTest::qWait(80);
        check("keyboard action reveals controls and cursor", bar->isVisible() && window.cursor().shape() != Qt::BlankCursor);
        QTest::mousePress(video, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100)); key(Qt::Key_Escape);
        check("first Escape cancels video gesture only", window.isFullScreen() && !video->gestureActive());
        QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        auto *slider = widget<QSlider>("seekSlider");
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, slider->rect().center()); QTest::keyClick(slider, Qt::Key_Escape);
        check("first Escape cancels timeline without leaving fullscreen", !slider->isSliderDown() && window.isFullScreen());
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, slider->rect().center());
        auto *play = widget<QToolButton>("playPauseButton"); play->setFocus(); QTest::qWait(2700);
        check("focused controls prevent auto hide", bar->isVisible()); video->setFocus();
        QMenu menu(&window); menu.addAction(action("play.toggle")); menu.popup(video->mapToGlobal(QPoint(100, 100))); QTest::qWait(2700);
        check("open menu prevents auto hide", bar->isVisible()); QTest::keyClick(&menu, Qt::Key_Escape); QTest::qWait(50);
        check("menu Escape closes one layer", !menu.isVisible() && window.isFullScreen()); screenshot("fullscreen-visible");
        key(Qt::Key_Escape); QTest::qWait(100);
        for (int i = 0; i < 20; ++i) { action("window.fullscreen")->trigger(); QTest::qWait(15); action("window.fullscreen")->trigger(); QTest::qWait(15); }
        check("20 fullscreen cycles preserve context media pause and cursor", video->context() == context && loads == before && flag("pause") && !window.isFullScreen() && window.cursor().shape() != Qt::BlankCursor && bar->isVisible());
        const auto beforeOsd = video->grabFramebuffer(); action("view.mode.standard")->trigger();
        auto *osd = widget<QLabel>("playerOsd");
        check("OSD is a mouse-transparent sibling outside video FBO", osd->parentWidget() == video->parentWidget() && osd->testAttribute(Qt::WA_TransparentForMouseEvents));
        check("production OSD stays outside framebuffer and passes pointer to video", osd->isVisible() && video->grabFramebuffer() == beforeOsd && video->parentWidget()->childAt(osd->geometry().center()) == video);
    }
    void dropping()
    {
        drop({fixtures + "/grid.mp4", fixtures + "/audio.m4a"});
        require("multiple media drop opens first queued item", until([this] { return state == Player::State::Loaded && path.endsWith("grid.mp4"); }));
        player.setPaused(true); require("dropped item pauses", until([this] { return flag("pause"); }));
        const auto current = path;
        auto *queue = window.findChild<PlaylistController *>();
        check("multiple media drop retains every item", queue->model().rowCount() == 2);
        drop({"https://example.invalid/file.mp4"}); check("remote URL drop rejected", path == current && widget<QLabel>("playerOsd")->text().contains(QStringLiteral("本地")));
        drop({fixtures}); check("folder drop rejected explicitly", path == current && widget<QLabel>("playerOsd")->text().contains(QStringLiteral("文件夹")));
        drop({fixtures + "/grid.mp4"}, Qt::ControlModifier); check("Ctrl append deduplicates without replacing current", path == current && queue->model().rowCount() == 2);
        for (auto extension : {"srt", "ass", "vtt"}) {
            QSignalSpy loaded(&player, &Player::subtitleLoaded); drop({fixtures + "/external." + extension});
            require(QString("single %1 subtitle drop confirmed").arg(extension), until([&] { return loaded.count() == 1; }));
            check("subtitle drop preserves media and pause", path == current && flag("pause"));
        }
        drop({fixtures + "/portrait.mp4"}); require("single media drop opens file", until([this] { return state == Player::State::Loaded && path.endsWith("portrait.mp4"); }));
        auto *slider = widget<QSlider>("seekSlider"); QTest::qWait(100); QSignalSpy requests(&player, &Player::seekRequested);
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, slider->rect().center()); player.openFile(fixtures + "/broken.mkv");
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, slider->rect().center());
        require("broken media gives persistent error", until([this] { return state == Player::State::Error; }));
        check("replacement cancels timeline preview", !slider->isSliderDown() && requests.isEmpty());
        key(Qt::Key_F11); QTest::qWait(2700); check("error state keeps fullscreen controls visible", widget<QWidget>("controlBar")->isVisible()); key(Qt::Key_Escape);
        drop({fixtures + "/external.srt"}); check("subtitle without loaded media gives useful feedback", widget<QLabel>("playerOsd")->text().contains(QStringLiteral("请先")));
        open();
        for (const auto size : {QSize(1120, 720), QSize(800, 500), QSize(640, 400)}) { window.resize(size); QTest::qWait(60); screenshot(QString("m2-%1x%2").arg(size.width()).arg(size.height())); }
    }
    void run() { parser(); playback(); audioAndView(); mouse(); timelineAndDialogs(); fullscreen(); dropping(); }
    bool save()
    {
        bool passed = true; for (const auto &value : checks) passed &= value.toObject()["passed"].toBool();
        QFile file(output + "/results.json"); if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(QJsonObject{{"passed", passed}, {"devicePixelRatio", video->devicePixelRatioF()}, {"checks", checks}}).toJson()); return passed;
    }
};
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); app.setApplicationName("zopana"); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() != 3) return 2;
    try {
        Suite suite(app.arguments()[1], app.arguments()[2]);
        try { suite.run(); } catch (const std::exception &error) { suite.check("aborted", false, {{"reason", error.what()}}); }
        return suite.save() ? 0 : 1;
    } catch (const std::exception &error) { std::fprintf(stderr, "%s\n", error.what()); return 2; }
}
