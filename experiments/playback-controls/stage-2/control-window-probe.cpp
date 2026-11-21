#include "controlwindow.h"
#include "mainwindow.h"
#include "playbackdialogs.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
using Id = PlayerActions::Id;

void require(bool condition, const char *description)
{
    if (!condition) throw std::runtime_error(description);
    std::printf("PASS %s\n", description);
    std::fflush(stdout);
}

bool until(auto condition, int timeout = 6000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

bool near(double actual, double expected, double tolerance = .08)
{
    return std::abs(actual - expected) < tolerance;
}

Player &initialize(Player &player)
{
    require(player.initialize(), "initialize libmpv");
    return player;
}

void dragMove(QWidget *widget, QPointF position)
{
    QMouseEvent event(QEvent::MouseMove, position, widget->mapToGlobal(position.toPoint()),
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

class Suite
{
public:
    Suite(const QString &fixtures, const QString &output)
        : fixtures_(fixtures), output_(output), settings_(output + "/config"), history_(output + "/data"),
          window_(initialize(player_)), session_(player_, window_, settings_, history_)
    {
        window_.show();
        activate(&window_);
        require(until([&] { return video_->rendererReady(); }), "main video renderer ready");
        std::printf("Qt %s, %lld active screen(s), effective video DPR %.2f\n", qVersion(),
                    static_cast<long long>(QGuiApplication::screens().size()), video_->devicePixelRatioF());
        for (auto *screen : QGuiApplication::screens()) {
            const auto geometry = screen->geometry();
            std::printf("Screen %s: %dx%d logical, DPR %.2f\n", qPrintable(screen->name()),
                        geometry.width(), geometry.height(), screen->devicePixelRatio());
        }
        player_.setMuted(true);
    }

    void run()
    {
        lifecycle();
        for (const auto mode : {Id::FeatureMode, Id::StandardMode}) playback(mode);
        framing();
        window_.close();
        require(until([&] { return !window_.isVisible() && !small_->isVisible(); }), "closing the main player hides the independent control window");
        player_.shutdown();
    }

private:
    QAction *action(Id id) { return actions_->get(id); }
    QToolButton *button(const char *name) { return small_->findChild<QToolButton *>(name); }
    QLabel *label(const char *name) { return small_->findChild<QLabel *>(name); }
    QSlider *zoom() { return small_->findChild<QSlider *>("controlZoomSlider"); }
    QTabWidget *tabs() { return small_->findChild<QTabWidget *>("controlTabs"); }
    bool emptyLoop() { return label("controlLoopA")->text() == QStringLiteral("—") && label("controlLoopB")->text() == QStringLiteral("—"); }

    void activate(QWidget *window)
    {
        window->raise();
        window->activateWindow();
        require(until([&] { return QApplication::activeWindow() == window; }), "expected player window is active");
    }

    void showSmall()
    {
        action(Id::ControlWindowToggle)->setChecked(true);
        require(until([&] { return small_->isVisible(); }), "control window is visible");
        activate(small_);
    }

    void loadVideo()
    {
        ids_ = queue_->addFiles({fixtures_.filePath("../zopana-ui-m2-v1/long-grid.mp4"), fixtures_.filePath("audio.m4a")}, true);
        require(ids_.size() == 2 && until([&] {
            return player_.canSeek() && player_.itemId() == ids_[0] && zoom()->isEnabled() && !player_.paused();
        }), "long grid video starts with zoom and seeking available");
    }

    void lifecycle()
    {
        auto *toggle = action(Id::ControlWindowToggle);
        auto *toolbar = window_.findChild<QToolButton *>("controlWindowButton");
        auto *view = window_.findChild<QMenu *>("viewMenu");
        require(small_->isWindow() && view->actions().contains(toggle) && toolbar->defaultAction() == toggle
            && toggle->shortcut() == QKeySequence(Qt::Key_F7), "independent window, toolbar and View menu share the default F7 action");
        video_->setFocus();
        QTest::keyClick(video_, Qt::Key_F7);
        require(until([&] { return QApplication::activeWindow() == small_; }) && small_->isVisible()
            && toggle->isChecked() && tabs()->currentIndex() == 0 && tabs()->count() == 2, "F7 opens the single window on the playback tab");
        const QPoint original = small_->pos();
        small_->move(original + QPoint(-30, 25));
        require(small_->pos() != original && window_.findChildren<ControlWindow *>().size() == 1, "control window moves independently");
        small_->close();
        require(until([&] { return !small_->isVisible() && QApplication::activeWindow() == &window_ && video_->hasFocus(); })
            && !toggle->isChecked(), "closing the control window restores focus to the video");

        auto preferences = session_.preferences();
        preferences.shortcuts["play.toggle"] = QStringList{"F7"};
        require(session_.applyPreferences(preferences) && action(Id::PlayPause)->shortcut() == QKeySequence(Qt::Key_F7)
            && toggle->shortcuts().isEmpty(), "existing custom shortcut wins over the new F7 default");
        preferences.shortcuts.remove("play.toggle");
        preferences.shortcuts["window.controls"] = QStringList{"F8"};
        require(session_.applyPreferences(preferences) && toggle->shortcut() == QKeySequence(Qt::Key_F8), "control window shortcut can be rebound");
        toolbar->click();
        require(until([&] { return QApplication::activeWindow() == small_; }) && small_->isVisible(), "toolbar opens the same window");
        QSignalSpy toggles(toggle, &QAction::triggered);
        auto *focused = QApplication::focusWidget();
        require(focused && focused->window() == small_, "rebound shortcut runs from a focused control");
        QTest::keyClick(focused, Qt::Key_F8);
        require(until([&] { return !small_->isVisible(); }) && toggles.count() == 1, "custom shortcut closes the focused tool window once");
        activate(&window_);
        video_->setFocus();
        QTest::keyClick(video_, Qt::Key_F8);
        require(until([&] { return small_->isVisible(); }) && toggles.count() == 2, "custom shortcut opens the window from the main view once");
        small_->close();
        preferences.shortcuts.remove("window.controls");
        require(session_.applyPreferences(preferences), "restore default shortcut");

        activate(&window_);
        action(Id::Fullscreen)->trigger();
        QTest::keyClick(video_, Qt::Key_F7);
        require(until([&] { return window_.isFullScreen() && small_->isVisible() && QApplication::activeWindow() == small_; }),
            "control window opens above the full-screen player");
        window_.showMinimized();
        require(until([&] { return !small_->isVisible(); }) && toggle->isChecked(), "minimize hides the control window without losing its open state");
        window_.showFullScreen();
        require(until([&] { return small_->isVisible(); }), "restoring the full-screen player restores the control window");
        window_.showNormal();
        const QRect work = window_.screen()->availableGeometry();
        small_->move(work.bottomRight() + QPoint(1600, 1600));
        require(!work.intersects(small_->frameGeometry()), "position tool window outside the work area");
        window_.showMinimized();
        require(until([&] { return !small_->isVisible(); }), "hide off-screen tool with the main window");
        window_.showNormal();
        require(until([&] { return small_->isVisible() && work.contains(small_->frameGeometry()); }), "restore recovers an off-screen window into the available work area");
        small_->close();
    }

    void seekWith(QToolButton *control, int seconds)
    {
        const double target = player_.position() + seconds;
        QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, QPoint(12, control->height() / 2));
        require(player_.seekInProgress() && !button("controlSetA")->isEnabled() && !button("controlSetB")->isEnabled(),
                "setting A or B is disabled as soon as a control window seek begins");
        require(until([&] { return !player_.seekInProgress() && button("controlSetA")->isEnabled()
            && player_.paused() && near(player_.position(), target); }), "seek settles on the requested frame without unpausing");
    }

    void playback(Id mode)
    {
        action(mode)->trigger();
        auto preferences = session_.preferences();
        preferences.seekSmallSeconds = 3;
        preferences.seekMediumSeconds = 12;
        preferences.seekLargeSeconds = 45;
        require(session_.applyPreferences(preferences), "apply custom seek steps");
        loadVideo();
        showSmall();
        tabs()->setCurrentIndex(0);

        auto *play = button("controlPlayPause");
        require(play->defaultAction() == action(Id::PlayPause) && label("controlDuration")->text() == "00:02:17.000",
            "shared play button and current media duration displayed");
        play->setFocus(Qt::TabFocusReason);
        QTest::keyClick(play, Qt::Key_Tab);
        require(QApplication::focusWidget() && QApplication::focusWidget() != play
            && QApplication::focusWidget()->window() == small_, "Tab stays in the tool window");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab, Qt::ShiftModifier);
        require(play->hasFocus(), "Shift+Tab returns to the play button");
        QSignalSpy toggles(action(Id::PlayPause), &QAction::triggered);
        QTest::keyClick(play, Qt::Key_Space);
        require(until([&] { return player_.paused(); }) && toggles.count() == 1, "Space on focused play button pauses exactly once");

        auto *smallForward = button("controlForwardSmall");
        auto *smallBack = button("controlBackSmall");
        auto *largeForward = button("controlForwardLarge");
        auto *largeBack = button("controlBackLarge");
        require(largeForward->toolTip().contains("45") && smallForward->toolTip().contains("3")
            && smallBack->popupMode() == QToolButton::MenuButtonPopup && smallForward->popupMode() == QToolButton::MenuButtonPopup
            && smallBack->menu()->actions() == QList<QAction *>{action(Id::BackMedium)}
            && smallForward->menu()->actions() == QList<QAction *>{action(Id::ForwardMedium)}
            && action(Id::ForwardMedium)->toolTip().contains("12"), "four seek buttons and the two medium menus follow preferences");
        seekWith(largeForward, 45);
        seekWith(smallBack, -3);
        const double medium = player_.position() + 12;
        bool opened = false;
        QTimer::singleShot(80, small_, [&] {
            auto *menu = smallForward->menu();
            opened = menu->isVisible() && menu->actionGeometry(action(Id::ForwardMedium)).isValid();
            if (opened) QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action(Id::ForwardMedium)).center());
        });
        QTest::mouseClick(smallForward, Qt::LeftButton, Qt::NoModifier, QPoint(smallForward->width() - 5, smallForward->height() / 2));
        QTest::qWait(100);
        require(opened && until([&] { return !player_.seekInProgress() && near(player_.position(), medium); }), "medium split menu seeks by the configured step");
        seekWith(largeBack, -45);

        auto *rateMenu = window_.findChild<QMenu *>("rateMenu");
        auto *rateButton = window_.findChild<QToolButton *>("playbackRateButton");
        auto *rateUp = button("controlRateMore");
        require(rateButton->menu() == rateMenu && rateMenu->actions().contains(rateUp->defaultAction())
            && rateUp->defaultAction() == action(Id::RateMore), "main menu, bottom bar and control window share speed actions");
        if (mode == Id::FeatureMode) {
            for (int i = 0; i < 8; ++i) button("controlRateLess")->click();
            require(until([&] { return label("controlSpeed")->text() == QStringLiteral("0.25×"); }), "decreasing rate clamps at 0.25×");
            for (int i = 0; i < 14; ++i) rateUp->click();
            require(until([&] { return label("controlSpeed")->text() == QStringLiteral("3.00×"); }), "increasing rate clamps at 3×");
        }
        button("controlRateReset")->click();
        require(until([&] { return near(player_.mediaProperty("speed").toDouble(), 1, .001); }), "reset to 1× with the control window");
        rateUp->setFocus(Qt::TabFocusReason);
        QTest::keyClick(rateUp, Qt::Key_Space);
        require(until([&] { return label("controlSpeed")->text() == QStringLiteral("1.25×") && rateButton->text() == label("controlSpeed")->text()
            && rateMenu->title().contains(label("controlSpeed")->text()); }) && player_.paused() && toggles.count() == 1,
            "Space on the speed button only adjusts the confirmed rate across both windows");

        auto *loopB = button("controlSetB");
        auto *loopA = button("controlSetA");
        activate(small_);
        loopB->click();
        const double a = player_.position();
        require(until([&] { return label("controlLoopA")->text() == PlaybackDialogs::timestamp(a)
            && label("controlLoopB")->text() == QStringLiteral("—"); })
            && label("controlLoopNotice")->text().contains(QStringLiteral("请继续设置 B")), "B without A sets A and shows the existing prompt in the small window");
        player_.seek(a + .24);
        require(until([&] { return player_.canSetLoopPoint() && near(player_.position(), a + .24); }), "prepare a B point less than half a second after A");
        loopB->click();
        require(label("controlLoopB")->text() == QStringLiteral("—") && label("controlLoopNotice")->text().contains("0.5"), "half-second minimum rejects a short interval");
        seekWith(smallForward, 3);
        const double b = player_.position();
        loopB->click();
        require(until([&] { return player_.loopActive() && label("controlLoopB")->text() == PlaybackDialogs::timestamp(b); })
            && player_.paused() && near(player_.position(), b, .001), "valid B shows milliseconds and preserves the paused frame");
        require(small_->grab().save(output_.filePath(mode == Id::FeatureMode ? "playback.png" : "playback-standard.png")), "save playback tab evidence");
        play->click();
        require(until([&] { return !player_.paused() && player_.position() < b - 1; }), "resuming from the small window plays the A–B interval");
        play->click();
        require(until([&] { return player_.paused(); }), "pause again from the small window");
        loopA->click();
        require(until([&] { return label("controlLoopB")->text() == QStringLiteral("—") && !player_.loopActive(); }), "setting A again clears the old B");
        button("controlClearLoop")->click();
        require(until([&] { return emptyLoop(); }), "clear removes both endpoint labels");
        small_->close();
    }

    void framing()
    {
        window_.resize(820, 680);
        action(Id::RotateNext)->trigger();
        require(until([&] { return player_.videoRotation() == 90 && zoom()->isEnabled(); }), "rotate the paused frame before framing");
        showSmall();
        tabs()->setCurrentIndex(1);
        const double time = player_.position();
        const auto identity = player_.playbackIdentity();
        auto *right = button("panRight");
        auto *left = button("panLeft");
        auto *pad = small_->findChild<QWidget *>("controlPanPad");
        button("controlFit")->click();
        require(video_->zoomScale() == 1 && !right->isEnabled() && !pad->isEnabled(), "1× view disables panning on a fully fitted frame");
        button("controlZoomIn")->click();
        require(until([&] { return near(video_->zoomScale(), 1.15, .0001) && zoom()->value() == 115; }),
                "zoom plus uses the same 15 percent step as the main view");
        button("controlZoomOut")->click();
        require(until([&] { return near(video_->zoomScale(), 1, .0001); }), "zoom minus returns to the 1× lower bound");
        require(zoom()->minimum() == 100 && zoom()->maximum() == 800, "zoom slider covers 1× to 8×");
        zoom()->setFocus(Qt::TabFocusReason);
        for (int i = 0; i < 12; ++i) QTest::keyClick(zoom(), Qt::Key_PageUp);
        require(video_->zoomScale() == 4 && video_->viewFocus() == QPointF(.5, .5) && right->isEnabled()
            && label("controlZoomValue")->text() == QStringLiteral("4.00×"), "slider zooms from 1× to 4× around the main viewport center");
        QTest::mouseClick(right, Qt::LeftButton);
        require(video_->viewFocus().y() > .5, "right arrow moves the rotated picture to screen right");
        const double nudge = video_->viewFocus().y();
        QTest::mousePress(right, Qt::LeftButton);
        require(until([&] { return video_->viewFocus().y() > nudge; }), "holding an arrow pans continuously");
        require(until([&] { return !right->isEnabled(); }) && left->isEnabled(), "the reached edge disables only its direction");
        QTest::mouseRelease(right, Qt::LeftButton);
        QTest::mouseClick(left, Qt::LeftButton);
        require(right->isEnabled(), "reverse direction immediately leaves the edge");

        button("controlCenter")->click();
        QTest::mouseClick(left, Qt::LeftButton);
        const auto beforeDrag = video_->viewFocus();
        ViewGeometry expected;
        expected.setSizes(video_->size(), QSizeF(540, 960));
        expected.setFocus(4, {1 - beforeDrag.y(), beforeDrag.x()});
        const QPointF marker(.55, 212.0 / 540);
        const QPointF markerOnScreen = expected.topLeft()
            + QPointF((1 - marker.y()) * expected.frameSize().width(), marker.x() * expected.frameSize().height());
        const QPointF center(video_->width() / 2.0, video_->height() / 2.0);
        const auto press = pad->rect().center();
        const auto finish = press + (center - markerOnScreen);
        QTest::mousePress(pad, Qt::LeftButton, Qt::NoModifier, press);
        dragMove(pad, finish);
        QTest::mouseRelease(pad, Qt::LeftButton, Qt::NoModifier, finish.toPoint());
        require((video_->viewFocus() - marker).manhattanLength() < .005, "a direction button and pad drag center the off-center marker");
        const QImage frame = video_->grabFramebuffer();
        const QColor color = frame.pixelColor(frame.rect().center());
        require(color.red() > 190 && color.green() > 55 && color.green() < 175 && color.blue() < 125, "center pixel shows the orange fixture marker");
        require(frame.save(output_.filePath("marker-centered.png")) && small_->grab().save(output_.filePath("framing.png")), "save the centered frame and framing controls");
        const auto endedDrag = video_->viewFocus();
        dragMove(pad, press + QPoint(25, 15));
        require(video_->viewFocus() == endedDrag, "motion after mouse release is ignored");
        button("controlCenter")->click();
        require(video_->zoomScale() == 4 && video_->viewFocus() == QPointF(.5, .5) && player_.videoRotation() == 90, "Center retains 4× and the rotation");
        button("controlFit")->click();
        require(video_->zoomScale() == 1 && video_->viewFocus() == QPointF(.5, .5) && player_.videoRotation() == 90
            && player_.paused() && near(player_.position(), time, .001) && player_.playbackIdentity() == identity,
            "Fit keeps the rotation and the same paused frame");

        syncAndCancel();
    }

    void syncAndCancel()
    {
        auto *right = button("panRight");
        auto *pad = small_->findChild<QWidget *>("controlPanPad");
        action(Id::FeatureMode)->trigger();
        activate(&window_);
        video_->setFocus();
        const QPointF origin = video_->rect().center();
        QWheelEvent wheel(origin, video_->mapToGlobal(origin.toPoint()), {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(video_, &wheel);
        require(until([&] { return near(video_->zoomScale(), 1.15, .0001) && zoom()->value() == 115; }), "small window follows a wheel zoom in the main view");
        QTest::keyClick(video_, Qt::Key_R);
        require(zoom()->value() == 100 && !right->isEnabled(), "small window follows the main reset shortcut");
        zoom()->setValue(275);
        window_.resize(660, 690);
        require(until([&] { return right->isEnabled(); }), "narrow viewport allows horizontal panning");
        window_.resize(1100, 690);
        require(until([&] { return !right->isEnabled() && button("panDown")->isEnabled(); }) && zoom()->value() == 275,
            "resizing updates limits without requiring a zoom change");
        window_.resize(720, 690);
        require(until([&] { return right->isEnabled(); }), "narrow viewport restores horizontal direction");
        const auto start = video_->viewFocus();
        const QPoint mainPoint = video_->rect().center();
        QTest::mousePress(video_, Qt::LeftButton, Qt::NoModifier, mainPoint);
        dragMove(video_, mainPoint + QPoint(300, 0));
        QTest::mouseRelease(video_, Qt::LeftButton, Qt::NoModifier, mainPoint + QPoint(300, 0));
        require(start != video_->viewFocus() && !right->isEnabled() && button("panLeft")->isEnabled(), "main-view drag updates the small window edge states");

        activate(small_);
        button("controlCenter")->click();
        zoom()->setValue(400);
        QTest::mousePress(right, Qt::LeftButton);
        const auto beforeRepeat = video_->viewFocus();
        require(until([&] { return video_->viewFocus() != beforeRepeat; }), "start a repeated arrow gesture");
        activate(&window_);
        const auto afterFocusLoss = video_->viewFocus();
        QTest::qWait(180);
        require(!right->isDown() && video_->viewFocus() == afterFocusLoss, "losing focus stops arrow auto-repeat");
        QTest::mouseRelease(right, Qt::LeftButton);

        activate(small_);
        button("controlCenter")->click();
        QTest::mousePress(pad, Qt::LeftButton);
        small_->close();
        showSmall();
        const auto afterHide = video_->viewFocus();
        dragMove(pad, pad->rect().center() + QPoint(30, 25));
        QTest::mouseRelease(pad, Qt::LeftButton);
        require(video_->viewFocus() == afterHide, "hiding cancels an in-progress pad drag");

        tabs()->setCurrentIndex(0);
        button("controlSetA")->click();
        seekWith(button("controlForwardSmall"), 3);
        button("controlSetB")->click();
        require(until([&] { return player_.loopActive() && label("controlLoopA")->text() != QStringLiteral("—")
            && label("controlLoopB")->text() != QStringLiteral("—"); }), "both loop markers are set before switching media");
        tabs()->setCurrentIndex(1);
        QTest::mousePress(right, Qt::LeftButton);
        queue_->play(ids_[1]);
        require(!right->isDown() && until([&] { return player_.itemId() == ids_[1] && player_.canSeek(); })
            && !zoom()->isEnabled() && !pad->isEnabled() && emptyLoop(), "switching to audio cancels the gesture and clears framing and loop state");
        auto *play = button("controlPlayPause");
        require(play->isEnabled() && play->isVisible() && label("controlCurrentTime")->isVisible()
            && label("controlDuration")->isVisible(), "the framing tab still shows playback and times for audio");
        play->click();
        require(until([&] { return player_.paused(); }), "audio pauses using the framing tab header");
        tabs()->setCurrentIndex(0);
        require(button("controlForwardSmall")->isEnabled() && button("controlRateMore")->isEnabled(), "seek and speed remain available for seekable audio");
        QTest::mouseRelease(right, Qt::LeftButton);
        queue_->play(ids_[0]);
        require(until([&] { return player_.itemId() == ids_[0] && zoom()->isEnabled() && !right->isDown(); }), "video framing controls recover on the next video");
    }

    QDir fixtures_, output_;
    SettingsStore settings_;
    HistoryStore history_;
    Player player_;
    MainWindow window_;
    SessionController session_;
    PlayerActions *actions_ = window_.findChild<PlayerActions *>();
    ControlWindow *small_ = window_.findChild<ControlWindow *>();
    PlaylistController *queue_ = window_.findChild<PlaylistController *>();
    VideoWidget *video_ = window_.findChild<VideoWidget *>("videoWidget");
    QList<quint64> ids_;
};
}

int main(int argc, char **argv)
{
    std::setlocale(LC_NUMERIC, "C");
    QApplication app(argc, argv);
    app.setApplicationName("zopana control window probe");
    const auto arguments = app.arguments();
    if (arguments.size() != 3 || !QDir().mkpath(arguments[2])) return 2;
    try {
        Suite suite(arguments[1], arguments[2]);
        suite.run();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
