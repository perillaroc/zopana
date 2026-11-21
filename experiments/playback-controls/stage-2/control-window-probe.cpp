#include "captureservice.h"
#include "controlwindow.h"
#include "mainwindow.h"
#include "playbackdialogs.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSlider>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>
#include <QThreadPool>
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
        fullViewPreview();
        window_.close();
        require(until([&] { return !window_.isVisible() && !small_->isVisible(); }), "closing the main player hides the independent control window");
        player_.shutdown();
    }

    void verifyRestart()
    {
        require(!session_.preferences().resetViewOnOpen && action(Id::HoldFullView)->shortcuts().size() == 2,
                "the new process reads the saved bindings and inherits framing on file changes");
        player_.skipNextResume();
        player_.openFile(fixtures_.filePath("../zopana-ui-m2-v1/long-grid.mp4"));
        require(until([&] { return zoom()->isEnabled() && player_.videoRotation() == 90; })
            && video_->zoomScale() == 1 && video_->viewFocus() == QPointF(.5, .5),
            "a new process starts at 1× centered with the saved 90° rotation");
        window_.close();
        player_.shutdown();
    }

    void verifyClipboard(bool cropped)
    {
        auto *captures = window_.findChild<CaptureService *>();
        auto *clipboard = QApplication::clipboard();
        auto *copy = action(Id::CopyViewport);
        auto *copyButton = button("controlCopyViewport");
        auto *picture = window_.findChild<QMenu *>("pictureMenu");
        const QString sentinel = "zopana clipboard regression";
        showSmall();
        tabs()->setCurrentIndex(1);
        require(picture->actions().contains(copy) && copyButton->defaultAction() == copy
            && copy->shortcuts().isEmpty(), "the Picture menu and framing tab share Copy Current View");
        clipboard->setText(sentinel);
        const auto requireUnavailable = [&](const char *message) {
            copy->trigger();
            copyButton->click();
            require(!copy->isEnabled() && !copyButton->isEnabled() && captures->viewportImage().isNull()
                && clipboard->text() == sentinel, message);
        };
        requireUnavailable("an empty player leaves the clipboard untouched");

        activate(&window_);
        window_.resize(820, 680);
        action(Id::Playlist)->trigger();
        auto *search = window_.findChild<QLineEdit *>("playlistSearch");
        search->setFocus();
        search->setText(sentinel);
        search->selectAll();
        clipboard->setText("old text");
        QTest::keyClick(search, Qt::Key_C, Qt::ControlModifier);
        require(clipboard->text() == sentinel, "Ctrl+C still copies selected playlist search text");
        search->clear();
        action(Id::Playlist)->trigger();

        const auto openPaused = [&](const QString &file, quint64 id) {
            player_.openFile(fixtures_.filePath(file), id);
            player_.setInitialPaused(true);
            require(until([&] { return player_.itemId() == id && player_.paused()
                && player_.canSeek() && copy->isEnabled() && copyButton->isEnabled(); }),
                "a paused video enables both copy entries after its first frame");
        };
        if (cropped) {
            player_.openFile(fixtures_.filePath("audio.m4a"));
            require(until([&] { return player_.state() == Player::State::Loaded && player_.canSeek(); }), "load an audio-only clip");
            requireUnavailable("audio playback leaves the clipboard untouched");

            openPaused("grid.mp4", 1);
            const auto oldImage = captures->viewportImage().convertToFormat(QImage::Format_RGB32);
            player_.setRendererReady(false);
            requireUnavailable("renderer recovery disables copy immediately");
            player_.setRendererReady(true);
            require(until([&] { return copy->isEnabled() && !player_.recovering(); }), "copy recovers with a confirmed frame");

            player_.openFile(fixtures_.filePath("portrait.mp4"), 2);
            player_.openFile(fixtures_.filePath("motion-1080.mp4"), 3);
            player_.setInitialPaused(true);
            requireUnavailable("rapid replacement disables copy before the new frame");
            require(until([&] { return player_.itemId() == 3 && player_.paused() && copy->isEnabled(); }),
                "the final replacement can be copied once painted");
            copy->trigger();
            const auto replacement = clipboard->image().convertToFormat(QImage::Format_RGB32);
            require(!replacement.isNull() && replacement != oldImage
                && replacement == captures->viewportImage().convertToFormat(QImage::Format_RGB32),
                "the clipboard holds the replacement's frame instead of the original grid");
            require(replacement.save(output_.filePath("replacement.png")), "retain the frame copied after replacement");
            const auto green = replacement.pixelColor(replacement.width() / 4, replacement.height() / 2);
            require(green.green() > 160 && green.red() < 60 && green.blue() < 60, "the first copy contains the replacement's green color bar");
            clipboard->setText(sentinel);
        }

        openPaused("grid.mp4", 4);
        player_.seek(2.4);
        require(until([&] { return player_.paused() && !player_.seekInProgress() && near(player_.position(), 2.4, .001); }),
                "position the video on a known paused frame");
        if (cropped) {
            action(Id::RotateNext)->trigger();
            requireUnavailable("rotation disables copy until the new angle has painted");
            require(until([&] { return player_.videoRotation() == 90 && copy->isEnabled(); }), "the rotated frame is ready");
            zoom()->setValue(400);
            video_->restoreView(4, {.9, .5});
            require(video_->zoomScale() == 4 && near(video_->viewFocus().x(), .9, .001), "pan the rotated 4x crop to the subtitle");
        }
        const auto clean = captures->viewportImage().convertToFormat(QImage::Format_RGB32);
        require(!clean.isNull(), "capture a paused baseline before adding the subtitle");
        player_.addSubtitle(fixtures_.filePath("external.ass"));
        require(until([&] { return player_.mediaProperty("sid").toLongLong() > 0; }), "select the external ASS track");
        QImage expected;
        const bool visibleSubtitles = until([&] {
            expected = captures->viewportImage().convertToFormat(QImage::Format_RGB32);
            return !expected.isNull() && expected != clean;
        });
        if (!visibleSubtitles) expected.save(output_.filePath("subtitle-not-visible.png"));
        require(visibleSubtitles, "the paused viewport contains visible subtitle pixels");
        require(expected.size() == QSize(qRound(video_->width() * video_->devicePixelRatioF()),
            qRound(video_->height() * video_->devicePixelRatioF())), "the viewport uses physical pixels");
        if (!cropped) {
            const int x = expected.width() / 2;
            require(expected.pixelColor(x, 0) == Qt::black && expected.pixelColor(x, expected.height() - 1) == Qt::black
                && expected.pixelColor(x, expected.height() / 2) != Qt::black, "the fit viewport includes its black bars");
        }

        const auto directory = output_.filePath("screenshots");
        captures->setDirectory(directory);
        QSignalSpy saved(captures, &CaptureService::saved);
        QSignalSpy failed(captures, &CaptureService::failed);
        if (cropped) {
            activate(small_);
            QTest::mouseClick(copyButton, Qt::LeftButton);
        } else {
            activate(&window_);
            picture->popup(window_.mapToGlobal(QPoint(20, 30)));
            require(picture->isVisible(), "open the Picture menu");
            QTest::mouseClick(picture, Qt::LeftButton, Qt::NoModifier, picture->actionGeometry(copy).center());
        }
        auto *osd = window_.findChild<QLabel *>("playerOsd");
        const auto copied = clipboard->image().convertToFormat(QImage::Format_RGB32);
        require(copied == expected && osd->text() == QStringLiteral("已复制当前取景"),
            "copy puts the paused viewport in the clipboard and confirms success");
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        require(captures->lastSaved().isEmpty() && !QDir(directory).exists() && saved.isEmpty() && failed.isEmpty(),
            "copy alone produces no screenshot directory or saved path");

        action(Id::CaptureViewport)->trigger();
        require(until([&] { return !saved.isEmpty(); }) && failed.isEmpty(), "save the same viewport as a PNG");
        const auto lastSaved = captures->lastSaved();
        const auto png = QImage(lastSaved).convertToFormat(QImage::Format_RGB32);
        require(png.size() == copied.size() && png == copied, "clipboard and viewport PNG pixels are identical");
        captures->setDirectory(lastSaved);
        copy->trigger();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        require(clipboard->image().convertToFormat(QImage::Format_RGB32) == png
            && captures->lastSaved() == lastSaved && saved.size() == 1 && failed.isEmpty(),
            "copy succeeds with an unusable output directory and preserves the last saved path");
        require(QFile::copy(lastSaved, output_.filePath("viewport.png")) && copied.save(output_.filePath("clipboard.png")),
                "retain matching PNGs for the Windows Paint check");
        std::printf("Viewport: %dx%d pixels\n", copied.width(), copied.height());
        window_.close();
        player_.shutdown();
    }

private:
    QAction *action(Id id) { return actions_->get(id); }
    QToolButton *button(const char *name) { return small_->findChild<QToolButton *>(name); }
    QLabel *label(const char *name) { return small_->findChild<QLabel *>(name); }
    QSlider *zoom() { return small_->findChild<QSlider *>("controlZoomSlider"); }
    QTabWidget *tabs() { return small_->findChild<QTabWidget *>("controlTabs"); }
    QString loopTime(const char *name) { return button(name)->text().section(' ', -1); }
    bool emptyLoop() { return loopTime("controlSetA") == QStringLiteral("—") && loopTime("controlSetB") == QStringLiteral("—"); }

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
        require(!button("controlFullView")->isEnabled() && !action(Id::HoldFullView)->isEnabled(),
                "the hold action and button are disabled before video is available");
        require(small_->isWindow() && view->actions().contains(toggle) && toolbar->defaultAction() == toggle
            && toggle->shortcut() == QKeySequence(Qt::Key_F7), "independent window, toolbar and View menu share the default F7 action");
        video_->setFocus();
        QTest::keyClick(video_, Qt::Key_F7);
        require(until([&] { return QApplication::activeWindow() == small_; }) && small_->isVisible()
            && toggle->isChecked() && tabs()->currentIndex() == 0 && tabs()->count() == 2, "F7 opens the single window on the playback tab");
        auto *play = button("controlPlayPause");
        require(tabs()->widget(0)->isAncestorOf(play) && play->toolButtonStyle() == Qt::ToolButtonIconOnly
            && !label("controlCurrentTime") && !label("controlDuration"), "the playback tab contains the play icon with no shared time header");
        require(small_->grab().save(output_.filePath("playback-idle.png")), "save the idle playback layout");
        tabs()->setCurrentIndex(1);
        require(!play->isVisible() && !zoom()->isEnabled(), "the idle framing tab shows only its unavailable framing controls");
        require(small_->grab().save(output_.filePath("framing-idle.png")), "save the idle framing layout");
        tabs()->setCurrentIndex(0);
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

        action(Id::Fullscreen)->trigger();
        require(until([&] { return window_.isFullScreen(); }), "main player enters full screen");
        activate(&window_);
        video_->setFocus();
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
        auto *navigation = button("controlNavigationButton");
        require(play->defaultAction() == action(Id::PlayPause) && !play->icon().isNull()
            && !button("controlFrameBackward")->icon().isNull() && !button("controlFrameForward")->icon().isNull()
            && !navigation->icon().isNull() && navigation->menu()->actions().contains(action(Id::UndoSeek)),
            "the transport icons share playback actions and expose chapter navigation and undo");
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
        require(largeForward->text() == "45s" && smallForward->text() == "3s"
            && !largeForward->icon().isNull() && !smallBack->icon().isNull()
            && largeForward->accessibleName().contains("45") && smallForward->toolTip().contains("3")
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
        auto *rateDown = button("controlRateLess");
        auto *rateReset = button("controlRateReset");
        require(rateButton->menu() == rateMenu && rateMenu->actions().contains(rateUp->defaultAction())
            && rateUp->defaultAction() == action(Id::RateMore) && rateReset->defaultAction() == action(Id::RateReset)
            && !rateUp->icon().isNull() && !rateDown->icon().isNull(), "the plus, minus and rate display share the main speed actions");
        if (mode == Id::FeatureMode) {
            for (int i = 0; i < 8; ++i) rateDown->click();
            require(until([&] { return rateReset->text() == QStringLiteral("0.25×"); }), "decreasing rate clamps at 0.25×");
            for (int i = 0; i < 14; ++i) rateUp->click();
            require(until([&] { return rateReset->text() == QStringLiteral("3.00×"); }), "increasing rate clamps at 3×");
        }
        rateReset->click();
        require(until([&] { return near(player_.mediaProperty("speed").toDouble(), 1, .001)
            && rateReset->text() == QStringLiteral("1.00×"); }), "clicking the current rate resets it to 1×");
        rateUp->setFocus(Qt::TabFocusReason);
        QTest::keyClick(rateUp, Qt::Key_Space);
        require(until([&] { return rateReset->text() == QStringLiteral("1.25×") && rateButton->text() == rateReset->text()
            && rateMenu->title().contains(rateReset->text()); }) && player_.paused() && toggles.count() == 1,
            "Space on the speed button only adjusts the confirmed rate across both windows");

        auto *loopB = button("controlSetB");
        auto *loopA = button("controlSetA");
        auto *loopClear = button("controlClearLoop");
        require(emptyLoop() && loopA->text().startsWith('A') && loopB->text().startsWith('B')
            && qAbs(loopA->geometry().center().y() - loopB->geometry().center().y()) <= 2
            && qAbs(loopB->geometry().center().y() - loopClear->geometry().center().y()) <= 2
            && !loopClear->icon().isNull()
            && loopA->defaultAction() == action(Id::LoopA) && loopB->defaultAction() == action(Id::LoopB),
            "both endpoint buttons combine their timestamps and set actions in the same row");
        activate(small_);
        loopB->click();
        const double a = player_.position();
        require(until([&] { return loopTime("controlSetA") == PlaybackDialogs::timestamp(a)
            && loopTime("controlSetB") == QStringLiteral("—"); })
            && label("controlLoopNotice")->text().contains(QStringLiteral("请继续设置 B")), "B without A sets A and shows the existing prompt in the small window");
        player_.seek(a + .24);
        require(until([&] { return player_.canSetLoopPoint() && loopB->isEnabled()
            && near(player_.position(), a + .24); }), "prepare a B point less than half a second after A");
        activate(&window_);
        loopB->click();
        require(loopTime("controlSetB") == QStringLiteral("—") && label("controlLoopNotice")->isVisible()
            && label("controlLoopNotice")->text().contains("0.5"), "a short interval is rejected and the visible tool shows the hint");
        activate(small_);
        seekWith(smallForward, 3);
        const double b = player_.position();
        loopB->click();
        require(until([&] { return player_.loopActive() && loopTime("controlSetB") == PlaybackDialogs::timestamp(b)
            && loopB->accessibleDescription().contains(PlaybackDialogs::timestamp(b)); })
            && player_.paused() && near(player_.position(), b, .001) && !label("controlLoopNotice")->isVisible(),
            "valid B shows milliseconds on the endpoint button and preserves the paused frame");
        require(small_->grab().save(output_.filePath(mode == Id::FeatureMode ? "playback.png" : "playback-standard.png")), "save playback tab evidence");
        play->click();
        require(until([&] { return !player_.paused() && player_.position() < b - 1; }), "resuming from the small window plays the A–B interval");
        play->click();
        require(until([&] { return player_.paused(); }), "pause again from the small window");
        loopA->click();
        require(until([&] { return loopTime("controlSetB") == QStringLiteral("—") && !player_.loopActive(); }), "setting A again clears the old B");
        loopClear->click();
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
        require(video_->zoomScale() == 1 && !right->isEnabled() && pad->isEnabled(), "the pad remains available for zooming at 1×");
        const auto padCenter = pad->rect().center();
        QTest::mousePress(pad, Qt::LeftButton, Qt::NoModifier, padCenter);
        require(pad->cursor().shape() != Qt::ClosedHandCursor, "the fully fitted video cannot start a pad drag");
        QTest::mouseRelease(pad, Qt::LeftButton, Qt::NoModifier, padCenter);
        const auto wheelPad = [&](QPoint angle, QPoint pixels = {}) {
            QWheelEvent wheel(padCenter, pad->mapToGlobal(padCenter), pixels, angle, Qt::NoButton,
                Qt::NoModifier, Qt::NoScrollPhase, false);
            wheel.setAccepted(false);
            QApplication::sendEvent(pad, &wheel);
            return wheel.isAccepted();
        };
        require(wheelPad({0, 120}) && until([&] { return near(video_->zoomScale(), 1.3225, .0001)
            && zoom()->value() == 132; }), "scrolling the pad zooms from fit and updates the slider");
        require(wheelPad({}, {0, -120}) && until([&] { return near(video_->zoomScale(), 1.15, .0001); }),
            "pixel-based wheel input zooms out on the same pad in standard mode");
        button("controlFit")->click();
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
        activate(small_);
        const double nudge = video_->viewFocus().y();
        QSignalSpy repeats(right, &QToolButton::clicked);
        QTest::mousePress(right, Qt::LeftButton);
        require(until([&] { return repeats.size() >= 2 && video_->viewFocus().y() > nudge; }), "holding an arrow pans continuously");
        QTest::mouseRelease(right, Qt::LeftButton);

        const double pictureLeft = video_->framing().picture.left() / video_->devicePixelRatioF();
        video_->panBy({-pictureLeft - 6, 0});
        require(right->isEnabled(), "right arrow remains available one nudge from the edge");
        QTest::mouseClick(right, Qt::LeftButton);
        require(!right->isEnabled() && left->isEnabled(), "the reached edge disables only its direction");
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
        require(until([&] { return player_.loopActive() && loopTime("controlSetA") != QStringLiteral("—")
            && loopTime("controlSetB") != QStringLiteral("—"); }), "both loop markers are set before switching media");
        tabs()->setCurrentIndex(1);
        QTest::mousePress(right, Qt::LeftButton);
        queue_->play(ids_[1]);
        require(!right->isDown() && until([&] { return player_.itemId() == ids_[1] && player_.canSeek(); })
            && !zoom()->isEnabled() && !pad->isEnabled() && emptyLoop(), "switching to audio cancels the gesture and clears framing and loop state");
        require(!button("controlFullView")->isEnabled() && !action(Id::HoldFullView)->isEnabled(),
                "audio leaves both hold-to-fit inputs disabled");
        auto *play = button("controlPlayPause");
        require(!play->isVisible(), "the framing tab has no shared playback row");
        tabs()->setCurrentIndex(0);
        require(play->isVisible() && play->isEnabled(), "the playback icon is available for audio");
        play->click();
        require(until([&] { return player_.paused(); }), "audio pauses using the playback tab");
        require(button("controlForwardSmall")->isEnabled() && button("controlRateMore")->isEnabled(), "seek and speed remain available for seekable audio");
        QTest::mouseRelease(right, Qt::LeftButton);
        queue_->play(ids_[0]);
        require(until([&] { return player_.itemId() == ids_[0] && zoom()->isEnabled() && !right->isDown(); }), "video framing controls recover on the next video");
    }

    bool fitted() const { return video_->zoomScale() == 1 && video_->viewFocus() == QPointF(.5, .5); }

    void bindFullViewShortcut()
    {
        bool editorWorked = false;
        QTimer::singleShot(0, &window_, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialog->findChild<QTabWidget *>("settingsTabs")->setCurrentIndex(1);
            auto *filter = dialog->findChild<QLineEdit *>("shortcutFilter");
            filter->setFocus();
            QTest::keyClick(filter, Qt::Key_W);
            const bool filterWorked = filter->text() == "w" && video_->zoomScale() == 8;
            filter->setText(QStringLiteral("按住看全图"));
            auto *table = dialog->findChild<QTableWidget *>("shortcutTable");
            auto *first = dialog->findChild<QKeySequenceEdit *>("shortcutFirst");
            auto *second = dialog->findChild<QKeySequenceEdit *>("shortcutSecond");
            if (table->rowCount() == 1 && first->keySequence().isEmpty()) {
                first->setFocus();
                QTest::keyClick(first, Qt::Key_W);
                second->setFocus();
                QTest::keyClick(second, Qt::Key_J, Qt::ControlModifier);
                dialog->findChild<QPushButton *>("setBindingButton")->click();
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
                editorWorked = filterWorked && video_->zoomScale() == 8;
                filter->clear();
                filter->setFocus();
                QTest::keyClick(filter, Qt::Key_W);
                editorWorked &= filter->text() == "w" && video_->zoomScale() == 8;
            }
            dialog->reject();
        });
        action(Id::Settings)->trigger();
        require(editorWorked && action(Id::HoldFullView)->shortcuts() == QList<QKeySequence>{
            QKeySequence(Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_J)}, "settings records both bindings and keeps the filter editable");
    }

    void fullViewPreview()
    {
        const QPointF focus(.525, .432);
        auto *hold = button("controlFullView");
        require(action(Id::HoldFullView)->shortcuts().isEmpty() && action(Id::ResetView)->shortcut() == QKeySequence(Qt::Key_R),
                "hold-to-fit is unbound by default and R is still the one-shot fit");
        player_.seek(30.24);
        player_.setPaused(true);
        require(until([&] { return player_.paused() && !player_.seekInProgress() && near(player_.position(), 30.24, .001); }),
                "settle on one marked frame at 30.24 seconds");
        tabs()->setCurrentIndex(1);
        activate(small_);
        zoom()->setValue(800);
        video_->restoreView(8, focus);
        const auto unchanged = [&] {
            return video_->zoomScale() == 8 && (video_->viewFocus() - focus).manhattanLength() < .00001;
        };
        const auto samePausedFrame = [&] {
            return player_.paused() && near(player_.position(), 30.24, .001) && player_.videoRotation() == 90;
        };
        const auto markerVisible = [this](const QImage &image, double scale, QPointF center) {
            ViewGeometry framing;
            framing.setSizes(video_->size(), QSizeF(540, 960));
            framing.setFocus(scale, {1 - center.y(), center.x()});
            const QPointF point = framing.topLeft() + QPointF((1 - 212.0 / 540) * framing.frameSize().width(),
                                                            .55 * framing.frameSize().height());
            const QPoint pixel(qRound(point.x() * image.width() / video_->width()),
                               qRound(point.y() * image.height() / video_->height()));
            if (!image.rect().contains(pixel)) return false;
            const auto color = image.pixelColor(pixel);
            return color.red() > 190 && color.green() > 55 && color.green() < 175 && color.blue() < 125;
        };
        const auto before = video_->grabFramebuffer();
        require(hold->isEnabled() && samePausedFrame() && markerVisible(before, 8, focus),
                "the off-center orange marker is visible on the rotated 8× crop");
        QTest::mousePress(hold, Qt::LeftButton);
        const auto full = video_->grabFramebuffer();
        require(fitted() && hold->isDown() && zoom()->value() == 100 && markerVisible(full, 1, {.5, .5})
            && samePausedFrame(), "press shows the full rotated picture and keeps the paused frame");
        require(before.save(output_.filePath("before-hold.png")) && full.save(output_.filePath("during-hold.png"))
            && small_->grab().save(output_.filePath("hold-control.png")), "save the rotated crop, full picture and held control");
        dragMove(hold, {-8, -8});
        require(fitted(), "pointer motion outside the button keeps the preview until release");
        QTest::mouseRelease(hold, Qt::LeftButton);
        const auto restored = video_->grabFramebuffer();
        require(unchanged() && zoom()->value() == 800 && samePausedFrame() && restored == before
            && restored.save(output_.filePath("after-hold.png")), "mouse release restores the exact paused crop");

        bindFullViewShortcut();
        activate(&window_);
        video_->setFocus();
        QTest::keyPress(video_, Qt::Key_W);
        for (const auto type : {QEvent::KeyRelease, QEvent::KeyPress}) {
            QKeyEvent repeat(type, Qt::Key_W, Qt::NoModifier, "w", true);
            QApplication::sendEvent(video_, &repeat);
        }
        require(fitted() && video_->grabFramebuffer() == full, "holding the configured key shows the same frame through keyboard auto-repeat");
        QTest::keyRelease(video_, Qt::Key_W);
        require(unchanged() && samePausedFrame() && video_->grabFramebuffer() == before, "key release restores the exact paused crop");

        action(Id::Playlist)->setChecked(false);
        action(Id::Playlist)->trigger();
        auto *search = window_.findChild<QLineEdit *>("playlistSearch");
        search->setFocus();
        QTest::keyClick(search, Qt::Key_W);
        require(search->text() == "w" && unchanged(), "the playlist search receives a bound letter as text");
        search->clear();
        action(Id::Playlist)->trigger();
        video_->setFocus();

        QTest::keyPress(video_, Qt::Key_W);
        QTest::keyClick(video_, Qt::Key_Minus);
        const double reduced = 8 / 1.15;
        require(until([&] { return near(video_->zoomScale(), reduced, .00001); }), "zoom steps start from the saved 8× framing");
        QTest::keyPress(video_, Qt::Key_J, Qt::ControlModifier);
        QTest::keyRelease(video_, Qt::Key_W);
        require(fitted(), "release of the interrupted key leaves the new hold active");
        QTest::keyRelease(video_, Qt::Key_Control);
        QTest::keyRelease(video_, Qt::Key_J);
        require(near(video_->zoomScale(), reduced, .00001), "the new hold ends when its main key is released");

        video_->restoreView(8, focus);
        QTest::keyPress(video_, Qt::Key_W);
        const auto origin = video_->rect().center();
        QTest::mousePress(video_, Qt::LeftButton, Qt::NoModifier, origin);
        dragMove(video_, origin + QPoint(40, 30));
        QTest::mouseRelease(video_, Qt::LeftButton, Qt::NoModifier, origin + QPoint(40, 30));
        const auto moved = video_->viewFocus();
        QTest::keyRelease(video_, Qt::Key_W);
        require(video_->zoomScale() == 8 && moved != focus && video_->viewFocus() == moved,
                "a main video drag continues from the original zoom and survives the old key release");
        QTest::keyPress(video_, Qt::Key_W);
        QTest::keyClick(video_, Qt::Key_R);
        QTest::keyRelease(video_, Qt::Key_W);
        require(fitted(), "R stays at fit after an interrupted preview");
        video_->restoreView(8, focus);
        QTest::keyPress(video_, Qt::Key_W);
        QTest::keyClick(video_, Qt::Key_K, Qt::AltModifier);
        require(until([&] { return player_.videoRotation() == 180 && zoom()->isEnabled(); }) && unchanged(),
                "rotation restores framing before changing the angle");
        QTest::keyRelease(video_, Qt::Key_W);
        QTest::keyClick(video_, Qt::Key_K, Qt::AltModifier | Qt::ShiftModifier);
        require(until([&] { return player_.videoRotation() == 90 && zoom()->isEnabled(); }) && unchanged(),
                "late release and reverse rotation preserve the original content point");

        activate(small_);
        hold->setFocus();
        QTest::keyPress(hold, Qt::Key_Space);
        require(fitted(), "Space holds the focused control");
        button("controlCenter")->click();
        QTest::keyRelease(hold, Qt::Key_Space);
        require(video_->zoomScale() == 8 && video_->viewFocus() == QPointF(.5, .5),
                "Center keeps the saved 8× zoom and the old release leaves it centered");
        video_->restoreView(8, focus);
        QTest::mousePress(hold, Qt::LeftButton);
        tabs()->setCurrentIndex(0);
        require(!hold->isDown() && unchanged(), "leaving the framing tab ends the hold");
        QTest::mouseRelease(hold, Qt::LeftButton);
        tabs()->setCurrentIndex(1);
        QTest::mousePress(hold, Qt::LeftButton);
        small_->close();
        require(!hold->isDown() && unchanged(), "hiding the control window ends the hold");
        QTest::mouseRelease(hold, Qt::LeftButton);
        showSmall();
        QTest::mousePress(hold, Qt::LeftButton);
        activate(&window_);
        video_->setFocus();
        require(!hold->isDown() && unchanged(), "switching focus ends the mouse hold");
        QTest::keyPress(video_, Qt::Key_W);
        QTest::mouseRelease(hold, Qt::LeftButton);
        require(fitted(), "a late mouse release leaves the viewing shortcut active");
        activate(small_);
        require(unchanged(), "switching focus ends the viewing shortcut hold");
        QTest::keyRelease(video_, Qt::Key_W);

        activate(&window_);
        video_->setFocus();
        player_.setPaused(false);
        require(until([&] { return !player_.paused(); }), "resume the marked clip");
        const auto position = player_.position();
        QTest::keyPress(video_, Qt::Key_W);
        require(until([&] { return player_.position() > position + .2; }) && !player_.paused() && fitted(),
                "playback advances through the held full view");
        QTest::keyRelease(video_, Qt::Key_W);
        require(unchanged() && !player_.paused(), "release restores the crop while playback continues");

        const auto other = queue_->addFiles({fixtures_.filePath("grid.mp4")}, false).value(0);
        QTest::keyPress(video_, Qt::Key_W);
        queue_->play(other);
        require(until([&] { return player_.itemId() == other && zoom()->isEnabled(); }) && unchanged(),
                "switching clips inherits the pre-preview framing");
        QTest::keyRelease(video_, Qt::Key_W);
        require(unchanged(), "an old shortcut release leaves the new clip alone");

        auto preferences = session_.preferences();
        preferences.resetViewOnOpen = true;
        preferences.userRotationDeg = 90;
        require(session_.applyPreferences(preferences), "enable Fit on open");
        showSmall();
        QTest::mousePress(hold, Qt::LeftButton);
        queue_->play(ids_[0]);
        require(until([&] { return player_.itemId() == ids_[0] && zoom()->isEnabled(); }) && fitted() && !hold->isDown(),
                "Fit on open resets a clip change during a mouse hold");
        QTest::mouseRelease(hold, Qt::LeftButton);
        require(fitted(), "the old mouse release leaves the new clip at fit");

        video_->restoreView(8, focus);
        QTest::mousePress(hold, Qt::LeftButton);
        player_.setRendererReady(false);
        require(unchanged() && !hold->isDown() && !hold->isEnabled(), "renderer recovery clears the preview");
        player_.setRendererReady(true);
        require(until([&] { return hold->isEnabled(); }) && unchanged(), "the recovered renderer shows the saved framing");
        QTest::mouseRelease(hold, Qt::LeftButton);
        QTest::mousePress(hold, Qt::LeftButton);
        player_.stop();
        require(!hold->isDown() && !hold->isEnabled() && unchanged(), "stopping clears the preview and disables the control");
        QTest::mouseRelease(hold, Qt::LeftButton);
        preferences.resetViewOnOpen = false;
        require(session_.applyPreferences(preferences) && unchanged(), "retain the 8× framing before exiting with inheritance enabled");
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
    const auto mode = arguments.value(3);
    if ((arguments.size() != 3 && (arguments.size() != 4
        || (mode != "--restart" && mode != "--clipboard-fit" && mode != "--clipboard-crop")))
        || !QDir().mkpath(arguments[2])) return 2;
    try {
        Suite suite(arguments[1], arguments[2]);
        if (mode == "--restart") suite.verifyRestart();
        else if (!mode.isEmpty()) suite.verifyClipboard(mode == "--clipboard-crop");
        else suite.run();
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
