#include "mainwindow.h"
#include "playeractions.h"
#include "theme.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSet>
#include <QShortcut>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QDir>

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
    QString fixtures, output;
    Player::State state = Player::State::Idle;
    mpv_handle *observer = nullptr;
    QJsonArray checks, layouts;
    QJsonObject details;

    Suite(QString media, QString destination) : window(initialize()),
        fixtures(QDir::fromNativeSeparators(media)), output(QDir::fromNativeSeparators(destination))
    {
        QDir().mkpath(output);
        observer = mpv_create_client(player.handle(), "m1-test-observer");
        video = window.findChild<VideoWidget *>("videoWidget");
        QObject::connect(&player, &Player::mediaChanged, &window, [this](auto value, const QString &, const QString &) { state = value; });
        window.show(); window.raise(); window.activateWindow();
        require("renderer ready", until([this] { return video->rendererReady(); }));
        require("window active", QTest::qWaitForWindowActive(&window, 3000));
        details["devicePixelRatio"] = video->devicePixelRatioF();
        details["qt"] = qVersion();
        QJsonObject colors;
        for (auto [name, role] : {std::pair{"window", QPalette::Window}, {"surface", QPalette::Button},
                {"text", QPalette::WindowText}, {"secondary", QPalette::PlaceholderText},
                {"accent", QPalette::Highlight}, {"accentText", QPalette::HighlightedText}})
            colors[name] = window.palette().color(role).name();
        details["palette"] = colors;
    }

    Player &initialize() { if (!player.initialize()) throw std::runtime_error("Player initialization failed"); return player; }
    ~Suite() { player.shutdown(); if (observer) mpv_destroy(observer); }

    QAction *action(const QString &id) { return window.findChild<QAction *>(id); }
    template<class T> T *widget(const QString &name) { return window.findChild<T *>(name); }

    void check(QString name, bool passed, QJsonObject fields = {})
    {
        fields["name"] = name; fields["passed"] = passed; checks.append(fields);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData()); std::fflush(stdout);
    }
    void require(QString name, bool passed) { check(name, passed); if (!passed) throw std::runtime_error(name.toStdString()); }

    double number(const char *name)
    {
        double result = -100;
        mpv_get_property(observer, name, MPV_FORMAT_DOUBLE, &result);
        return result;
    }
    bool paused() { int value = 0; mpv_get_property(observer, "pause", MPV_FORMAT_FLAG, &value); return value != 0; }
    bool command(const QStringList &arguments)
    {
        std::vector<QByteArray> storage;
        for (const auto &argument : arguments) storage.push_back(argument.toUtf8());
        std::vector<const char *> args;
        for (const auto &argument : storage) args.push_back(argument.constData());
        args.push_back(nullptr);
        return mpv_command(observer, args.data()) >= 0;
    }
    void screenshot(const QString &name) { require("screenshot " + name, window.grab().save(output + '/' + name + ".png")); }

    void openFromKeyboard(const QString &name)
    {
        bool dialogSeen = false;
        QTimer::singleShot(150, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            dialogSeen = true;
            auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit");
            edit->setFocus(); QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(edit, fixtures + '/' + name);
            QTest::keyClick(edit, Qt::Key_Return);
        });
        video->setFocus(); QTest::keyClick(video, Qt::Key_O, Qt::ControlModifier);
        require("Ctrl+O keyboard file selection", dialogSeen && until([this] { return state == Player::State::Loaded; }));
        require("seek capability available", until([this] { return widget<QSlider>("seekSlider")->isEnabled(); }));
    }

    void layoutMatrix(const QString &stateName)
    {
        for (const auto size : {QSize(1120, 720), QSize(800, 500), QSize(640, 400)}) {
            window.resize(size); QTest::qWait(80);
            auto *bar = widget<QWidget>("controlBar");
            const QString tag = QString("%1-%2x%3").arg(stateName).arg(size.width()).arg(size.height());
            bool contained = true, disjoint = true, named = true, large = true;
            contained &= window.size() == size;
            const auto buttons = bar->findChildren<QToolButton *>();
            QList<QRect> rectangles;
            for (auto *button : buttons) {
                if (!button->isVisibleTo(&window)) continue;
                const QRect rectangle(button->mapTo(bar, QPoint()), button->size());
                contained &= bar->rect().contains(rectangle);
                named &= !button->accessibleName().isEmpty() && !button->toolTip().isEmpty();
                large &= button->width() >= 32 && button->height() >= 32;
                for (const auto &other : rectangles) disjoint &= !other.intersects(rectangle);
                rectangles.append(rectangle);
            }
            auto *seek = widget<QSlider>("seekSlider");
            contained &= bar->rect().contains(QRect(seek->mapTo(bar, QPoint()), seek->size()));
            for (auto *label : {widget<QLabel>("currentTimeLabel"), widget<QLabel>("durationTimeLabel")})
                contained &= label->width() >= label->fontMetrics().horizontalAdvance(label->text());
            check(tag + " layout fits", contained && disjoint, {{"barWidth", bar->width()}, {"buttonCount", rectangles.size()}});
            check(tag + " accessible controls", named && large);
            check(tag + " custom window bar", window.windowFlags().testFlag(Qt::FramelessWindowHint)
                && widget<QToolButton>("fullscreenButton")->parentWidget() == widget<QWidget>("playerTopBar"));
            auto *volume = widget<QSlider>("volumeSlider");
            check(tag + " both sliders remain on one row", volume->isVisible() && seek->geometry().center().y() == volume->geometry().center().y()
                && seek->geometry().right() < volume->geometry().left());
            int lastX = -1;
            bool ordered = true;
            for (auto name : {"playPauseButton", "stopButton", "backButton", "forwardButton", "openButton", "currentTimeLabel", "durationTimeLabel"}) {
                auto *control = widget<QWidget>(name);
                ordered &= control->isVisible() && lastX < control->x();
                lastX = control->x();
            }
            check(tag + " playback buttons and times stay in order", ordered);
            screenshot(tag);
            layouts.append(QJsonObject{{"state", stateName}, {"width", size.width()}, {"height", size.height()}, {"buttonsFit", contained && disjoint}});
        }
        window.resize(1120, 720); QTest::qWait(80);
    }

    void wheel(int steps)
    {
        const QPoint point = video->rect().center();
        QWheelEvent event(point, video->mapToGlobal(point), {}, {0, 120 * steps}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(video, &event); QTest::qWait(150);
    }

    void run()
    {
        const auto ids = {"file.open", "file.retry", "play.toggle", "view.reset", "window.fullscreen", "window.escape",
                          "view.mode.feature", "view.mode.standard", "app.quit", "window.about", "window.licenses"};
        bool unique = true, repeat = true;
        for (auto id : ids) { unique &= window.findChildren<QAction *>(id).size() == 1; repeat &= !action(id)->autoRepeat(); }
        check("one stable action per command", unique && repeat);
        check("legacy QShortcuts removed", window.findChildren<QShortcut *>().isEmpty());
        check("button uses shared action", widget<QToolButton>("playPauseButton")->defaultAction() == action("play.toggle"));
        check("empty state disables media actions", !action("play.toggle")->isEnabled() && !action("view.reset")->isEnabled() && !widget<QSlider>("seekSlider")->isEnabled());
        check("unknown duration displayed", widget<QLabel>("durationTimeLabel")->text() == "--:--");
        layoutMatrix("idle");
        openFromKeyboard("grid.mp4");
        QTest::keyClick(video, Qt::Key_Space);
        require("Space pauses", until([this] { return paused(); }));
        layoutMatrix("paused");
        check("video reset capability enabled", action("view.reset")->isEnabled());
        auto *seek = widget<QSlider>("seekSlider");
        seek->setFocus(Qt::TabFocusReason); const double before = number("time-pos");
        QTest::keyClick(seek, Qt::Key_Right);
        check("focused seek slider uses the five-second default", until([&] { return std::abs(number("time-pos") - before - 5) < .1; }));
        check("keyboard navigation marks slider focus", seek->property("keyboardFocus").toBool());
        screenshot("keyboard-focus");
        QTest::mousePress(seek, Qt::LeftButton);
        check("mouse operation clears keyboard-only focus styling", !seek->property("keyboardFocus").toBool());
        QTest::keyClick(seek, Qt::Key_Escape);
        QTest::mouseRelease(seek, Qt::LeftButton);

        QSignalSpy triggered(action("play.toggle"), &QAction::triggered);
        auto *play = widget<QToolButton>("playPauseButton"); play->setFocus();
        QTest::keyClick(play, Qt::Key_Space);
        check("focused button Space triggers exactly once", triggered.count() == 1);
        require("button resumes", until([this] { return !paused(); }));
        video->setFocus(); QTest::keyClick(video, Qt::Key_Space); until([this] { return paused(); });
        triggered.clear();
        QKeyEvent repeated(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true, 1);
        QApplication::sendEvent(video, &repeated); QTest::qWait(40);
        check("play toggle ignores auto-repeat", triggered.isEmpty() && paused());

        auto *edit = new QLineEdit(&window); edit->setGeometry(30, 60, 260, 32); edit->show(); edit->raise(); edit->setFocus();
        QTest::keyClicks(edit, "r R "); QTest::keyClick(edit, Qt::Key_Return);
        QInputMethodEvent preedit(QString::fromUtf8("中"), {}); QApplication::sendEvent(edit, &preedit);
        QTest::keyClick(edit, Qt::Key_Space);
        check("editor and IME preserve native input", triggered.isEmpty() && !window.isFullScreen() && edit->text().startsWith("r R "));
        QInputMethodEvent commit; QApplication::sendEvent(edit, &commit); delete edit;
        // Exercise native list semantics before the production playlist exists.
        auto *queue = new QListWidget(&window); queue->addItem("M1 input fixture");
        queue->setGeometry(30, 60, 260, 100); queue->show(); queue->raise(); queue->setCurrentRow(0); queue->setFocus();
        QSignalSpy activated(queue, &QListWidget::itemActivated);
        QTest::keyClick(queue, Qt::Key_Return); QTest::keyClick(queue, Qt::Key_Space);
        check("queue context retains native Enter and Space", activated.count() == 1 && triggered.isEmpty() && !window.isFullScreen() && paused());
        delete queue;
        video->setFocus();
        QSet<QString> visited;
        for (int i = 0; i < 24; ++i) {
            auto *focus = QApplication::focusWidget(); if (focus) visited.insert(focus->objectName());
            QTest::keyClick(focus ? focus : video, Qt::Key_Tab);
        }
        bool reachable = true;
        for (auto name : {"videoWidget", "seekSlider", "openButton", "playPauseButton", "featureModeButton", "standardModeButton", "resetViewButton", "volumeButton", "fullscreenButton", "moreButton"})
            if (widget<QWidget>(name)->isEnabled()) reachable &= visited.contains(name);
        check("Tab reaches existing playback controls", reachable, {{"visited", QJsonArray::fromStringList(visited.values())}});
        video->setFocus(); const auto fit = video->grabFramebuffer(); wheel(5);
        check("feature zoom changes view", video->grabFramebuffer() != fit);
        QTest::keyClick(video, Qt::Key_R); QTest::qWait(150);
        check("R restores fit view", video->grabFramebuffer() == fit);
        player.setVolume(50); until([this] { return std::abs(number("volume") - 50) < .1; });
        action("view.mode.standard")->trigger(); wheel(1);
        check("standard wheel on silent video leaves volume and view unchanged", std::abs(number("volume") - 50) < .1 && video->grabFramebuffer() == fit);
        action("view.mode.feature")->trigger(); wheel(1);
        check("feature wheel preserves volume", std::abs(number("volume") - 50) < .1 && video->grabFramebuffer() != fit);
        action("view.reset")->trigger();

        auto *context = video->context();
        const QRect normalGeometry = window.geometry();
        video->setFocus(); QTest::keyClick(video, Qt::Key_F11); QTest::qWait(100);
        check("F11 enters fullscreen", window.isFullScreen()); screenshot("fullscreen");
        QMenu menu(&window); menu.addAction(action("play.toggle")); menu.popup(video->mapToGlobal(QPoint(50, 50))); QTest::qWait(30);
        QTest::keyClick(&menu, Qt::Key_Escape); QTest::qWait(30);
        check("menu Escape does not leave fullscreen", !menu.isVisible() && window.isFullScreen());
        QTest::keyClick(video, Qt::Key_Escape); QTest::qWait(100);
        check("Escape exits fullscreen without context rebuild", !window.isFullScreen() && video->context() == context && paused());
        check("fullscreen exit restores normal geometry", !window.isMaximized() && window.geometry() == normalGeometry);
        video->setFocus(); QTest::keyClick(video, Qt::Key_Return); QTest::qWait(60);
        check("watching Enter enters fullscreen", window.isFullScreen()); QTest::keyClick(video, Qt::Key_Escape);

        auto *maximize = widget<QToolButton>("windowMaximizeButton");
        auto *fullscreen = widget<QToolButton>("fullscreenButton");
        QTest::mouseClick(maximize, Qt::LeftButton);
        require("top bar maximizes window", until([this] { return window.isMaximized(); }));
        QTest::mouseClick(fullscreen, Qt::LeftButton);
        require("top bar enters fullscreen from maximized", until([this] { return window.isFullScreen(); }));
        QTest::keyClick(video, Qt::Key_Escape);
        check("fullscreen exit restores maximized state", until([this] { return window.isMaximized() && !window.isFullScreen(); }));
        QTest::mouseClick(maximize, Qt::LeftButton);
        check("top bar restores previous normal geometry", until([&] { return !window.isMaximized() && window.geometry() == normalGeometry; }));

        bool licenseSeen = false, modalSafe = false;
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = QApplication::activeModalWidget();
            if (!dialog) return;
            auto *text = dialog->findChild<QPlainTextEdit *>();
            licenseSeen = text && text->isReadOnly() && text->toPlainText().contains("ISC License") && text->toPlainText().contains("The MIT License");
            text->setFocus(); QTest::keyClick(text, Qt::Key_Space); QTest::keyClick(text, Qt::Key_Return); QTest::keyClick(text, Qt::Key_F11);
            modalSafe = !window.isFullScreen() && paused(); QTest::keyClick(dialog, Qt::Key_Escape);
        });
        action("window.licenses")->trigger();
        check("complete ISC and MIT available in application", licenseSeen && modalSafe);

        auto *theme = window.findChild<Theme::Controller *>(); theme->apply(true);
        QTest::qWait(40);
        check("high contrast removes custom stylesheet", qApp->styleSheet().isEmpty()); screenshot("system-palette");
        const auto menuBackground = window.grab().toImage().pixelColor(2, 2);
        bool nativeBackground = false;
        for (auto role : {QPalette::Window, QPalette::Button, QPalette::Base}) nativeBackground |= menuBackground == qApp->palette().color(role);
        check("high contrast paints system menu background", nativeBackground, {{"rendered", menuBackground.name()}});
        check("high contrast icons available", !action("play.toggle")->icon().isNull());
        const auto icon = play->icon().pixmap({20, 20}).toImage();
        bool nativeInk = false;
        for (int y = 0; y < icon.height(); ++y) for (int x = 0; x < icon.width(); ++x)
            nativeInk |= icon.pixelColor(x, y).alpha() > 200 && icon.pixelColor(x, y).rgb() == play->palette().color(QPalette::ButtonText).rgb();
        check("high contrast primary icon uses native text color", nativeInk);
        theme->apply(false); QTest::qWait(40);
        check("dark theme restored", !qApp->styleSheet().isEmpty() && window.grab().toImage().pixelColor(2, 2) == QColor(Theme::Surface));

        player.openFile(fixtures + "/audio.m4a");
        require("audio loaded", until([this] { return state == Player::State::Loaded; })); QTest::qWait(100);
        check("audio disables video reset", !action("view.reset")->isEnabled()); screenshot("audio");
        player.openFile(fixtures + "/broken.mkv");
        check("loading state shown synchronously", widget<QLabel>("cardTitle")->text().contains(QStringLiteral("正在打开")));
        screenshot("loading");
        require("corrupt file errors", until([this] { return state == Player::State::Error; }));
        auto *card = widget<QWidget>("stateCard");
        const auto point = card->mapTo(&window, QPoint(10, 10));
        const auto errorFrame = window.grab();
        const QPoint physical(qRound(point.x() * errorFrame.devicePixelRatio()), qRound(point.y() * errorFrame.devicePixelRatio()));
        check("error page retains dark background after theme restore", errorFrame.toImage().pixelColor(physical) == QColor(Theme::Window));
        check("error clears old capabilities", !action("play.toggle")->isEnabled() && !action("view.reset")->isEnabled() && !seek->isEnabled() && action("file.retry")->isEnabled());
        layoutMatrix("error");
        openFromKeyboard("grid.mp4");
        command({"stop", "keep-playlist"}); require("external stop reaches idle", until([this] { return state == Player::State::Idle; }));
        check("stopped state is replayable", widget<QLabel>("cardTitle")->text() == QStringLiteral("已停止") && action("play.toggle")->isEnabled()); screenshot("stopped");
        action("play.toggle")->trigger(); require("replay from stopped", until([this] { return state == Player::State::Loaded; }));
        command({"set", "speed", "8"}); command({"seek", "15.7", "absolute+exact"}); command({"set", "pause", "no"});
        require("natural EOF reached", until([this] { return state == Player::State::Ended; }));
        check("EOF state is replayable", action("play.toggle")->isEnabled() && widget<QLabel>("cardTitle")->text() == QStringLiteral("播放完毕")); screenshot("ended");
    }

    bool save()
    {
        bool passed = true; for (const auto &check : checks) passed &= check.toObject()["passed"].toBool();
        QFile file(output + "/results.json"); if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(QJsonObject{{"passed", passed}, {"details", details}, {"checks", checks}, {"layouts", layouts}}).toJson());
        return passed;
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
