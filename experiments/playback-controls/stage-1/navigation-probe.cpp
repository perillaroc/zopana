#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QTest>
#include <QToolButton>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
using Id = PlayerActions::Id;

void check(bool result, const char *description)
{
    if (!result) throw std::runtime_error(description);
    std::printf("PASS %s\n", description);
}

bool until(auto predicate, int timeout = 6000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) QTest::qWait(10);
    return predicate();
}

bool near(double actual, double expected, double tolerance = .001)
{
    return std::abs(actual - expected) < tolerance;
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("initialize libmpv");
    return player;
}

struct Probe
{
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow main;
    SessionController session;
    PlayerActions *actions = main.findChild<PlayerActions *>();
    PlaylistController *playlist = main.findChild<PlaylistController *>();
    VideoWidget *video = main.findChild<VideoWidget *>("videoWidget");
    QSlider *slider = main.findChild<QSlider *>("seekSlider");
    QWidget *control = main.findChild<QWidget *>("controlWindow");
    QToolButton *navigation = control->findChild<QToolButton *>("controlNavigationButton");
    QToolButton *more = main.findChild<QToolButton *>("moreButton");
    QMenu *play = main.findChild<QMenu *>("playMenu");
    QMenu *chapters = play->findChild<QMenu *>("chaptersMenu");
    QAction *undo = actions->get(Id::UndoSeek);
    QSignalSpy requests{&player, &Player::seekRequested};
    QDir media, evidence;
    QList<quint64> ids;

    Probe(const QString &fixtures, const QString &output)
        : settings(output + "/config"), history(output + "/data"), main(initialize(player)),
          session(player, main, settings, history), media(fixtures), evidence(output)
    {
        main.show();
        activate(main);
        check(until([&] { return video->rendererReady(); }), "OpenGL widget is ready");
        std::printf("Qt %s, DPR %.2f\n", qVersion(), video->devicePixelRatioF());
        auto preferences = session.preferences();
        preferences.seekSmallSeconds = 1;
        check(session.applyPreferences(preferences), "one-second step for held-key checks");
        ids = playlist->addFiles({media.filePath("chapters.mkv"), media.filePath("no-chapters.mkv")}, true);
        check(ids.size() == 2, "opened both media fixtures in the queue");
        pauseItem(ids.front());
    }

    void activate(QWidget &window)
    {
        window.raise(); window.activateWindow();
        if (!QTest::qWaitForWindowActive(&window)) throw std::runtime_error("cannot activate probe window");
    }

    void pauseItem(quint64 id)
    {
        check(until([&] { return player.itemId() == id && player.canSeek() && video->canCapture(); }), "current item is loaded and seekable");
        player.setPaused(true);
        check(until([&] { return player.paused(); }), "paused state confirmed");
    }

    void at(double time, const char *description)
    {
        const bool matches = until([&] { return !player.seekInProgress() && near(player.position(), time) && player.paused(); });
        std::printf("POS %.6f expected %.6f paused=%d chapter=%lld\n", player.position(), time,
                    player.paused(), static_cast<long long>(player.currentChapter() + 1));
        check(matches, description);
    }

    void prime(double time)
    {
        if (!player.seekToStart(time, Player::StartPosition::Beginning)) throw std::runtime_error("cannot position fixture");
        at(time, "automatic setup seek settled while paused");
    }

    void requested(double time, const char *description)
    {
        check(requests.size() == 1 && near(requests.front().front().toDouble(), time, 1e-7), description);
        requests.clear();
    }

    bool highlighted(qsizetype index) const
    {
        const auto entries = chapters->actions();
        for (qsizetype i = 0; i < entries.size(); ++i)
            if (entries[i]->isChecked() != (i == index)) return false;
        return index >= 0 && index < entries.size();
    }

    void choose(QToolButton &button, QList<QAction *> path)
    {
        bool selected = false;
        QTimer::singleShot(60, &button, [&] {
            auto *menu = button.menu();
            for (auto *entry : path) {
                if (!menu->isVisible() || !menu->actions().contains(entry) || !entry->isEnabled()) {
                    std::fprintf(stderr, "Menu item %s unavailable in %s (visible=%d, enabled=%d)\n",
                                 qPrintable(entry->text()), qPrintable(menu->objectName()), menu->isVisible(), entry->isEnabled());
                    break;
                }
                if (entry == path.back()) {
                    QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(entry).center());
                    selected = true;
                    break;
                }
                menu->setActiveAction(entry);
                QTest::keyClick(menu, Qt::Key_Right);
                menu = entry->menu();
                if (!until([&] { return menu->isVisible(); }, 800)) {
                    std::fprintf(stderr, "Submenu %s did not open\n", qPrintable(menu->objectName()));
                    break;
                }
            }
            while (auto *popup = QApplication::activePopupWidget()) popup->close();
        });
        QTest::mouseClick(&button, Qt::LeftButton);
        check(selected, "selected the action through the visible popup menus");
    }

    QPoint marker(double time) const
    {
        QStyleOptionSlider option;
        option.initFrom(slider);
        option.orientation = Qt::Horizontal;
        option.minimum = slider->minimum(); option.maximum = slider->maximum();
        option.sliderPosition = slider->sliderPosition(); option.sliderValue = slider->value();
        const auto groove = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, slider);
        const auto handle = slider->style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
        const int x = groove.left() + handle.width() / 2 + qRound((groove.width() - handle.width()) * time / player.duration());
        return {x, slider->height() - 4};
    }

    QImage frame()
    {
        QTest::qWait(50);
        return video->grabFramebuffer();
    }

    void chapterNavigation()
    {
        mpv_node rawChapters{};
        check(mpv_get_property(player.handle(), "chapter-list", MPV_FORMAT_NODE, &rawChapters) == 0, "read raw chapter property for comparison");
        const auto raw = nodeValue(rawChapters).toList();
        mpv_free_node_contents(&rawChapters);
        check(raw.size() == 4 && raw.back().toMap().value("time").toDouble() > player.duration()
            && until([&] { return chapters->actions().size() == 3; }), "out-of-range chapter is excluded from the menu");
        const auto &list = player.chapters();
        check(list[0].title == QStringLiteral("开场 & 引子") && list[1].title == QStringLiteral("第 2 章")
            && near(list[1].start, 2, 1e-7) && near(list[2].start, 5.4, 1e-7), "titles, fallback label and fractional starts are retained");
        check(play->actions().contains(chapters->menuAction()) && navigation->menu()->actions().contains(chapters->menuAction())
            && play->actions().contains(undo) && navigation->menu()->actions().contains(undo), "the two windows share the chapter list and undo action");
        prime(0);
        check(highlighted(0) && !actions->get(Id::PreviousChapter)->isEnabled() && actions->get(Id::NextChapter)->isEnabled(), "first chapter and previous boundary");
        choose(*more, {play->menuAction(), actions->get(Id::NextChapter)});
        at(2, "next chapter reaches its start");
        check(highlighted(1), "the exact 2.00 s boundary selects chapter two");
        const QImage original = frame();
        actions->get(Id::ControlWindowToggle)->trigger();
        activate(*control);
        requests.clear();
        choose(*navigation, {chapters->menuAction(), chapters->actions()[2]});
        requested(5.4, "control-window chapter three submits 5.400000 s");
        check(highlighted(1), "pending chapter seek keeps the confirmed menu selection");
        at(5.4, "control-window chapter reaches 5.40 s and remains paused");
        check(highlighted(2) && actions->get(Id::PreviousChapter)->isEnabled() && !actions->get(Id::NextChapter)->isEnabled(), "last chapter and next boundary");
        const QImage destination = frame();
        activate(main);
        choose(*more, {play->menuAction(), undo});
        requested(2, "main menu undo submits the original 2.000000 s");
        at(2, "main menu undo reaches the original paused time");
        const QImage restored = frame();
        check(!original.isNull() && original != destination && original == restored && highlighted(1) && !undo->isEnabled(),
              "cross-window undo restores the original rendered frame, chapter and disabled undo state");
        check(original.save(evidence.filePath("chapter-2-original.png")) && destination.save(evidence.filePath("chapter-3.png"))
            && restored.save(evidence.filePath("chapter-2-restored.png")), "saved three Qt framebuffer captures");

        prime(5.36);
        requests.clear();
        choose(*more, {play->menuAction(), chapters->menuAction(), chapters->actions()[2]});
        requested(5.4, "main chapter menu seeks across the 40 ms boundary");
        at(5.4, "main chapter menu settles on the exact boundary");
        check(highlighted(2), "chapter three highlights at 5.40 s");
        choose(*more, {play->menuAction(), actions->get(Id::PreviousChapter)});
        at(2, "previous chapter reaches chapter two");
        requests.clear();
        const QPoint point = marker(5.4);
        QMouseEvent move(QEvent::MouseMove, point, slider->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(slider, &move);
        auto *hover = main.findChild<QLabel *>("seekHoverTime");
        check(hover->text().contains(QStringLiteral("00:00:05.400")) && hover->text().contains(QStringLiteral("复核")), "chapter marker hover shows its fractional time and title");
        QTest::mouseClick(slider, Qt::LeftButton, {}, point);
        requested(5.4, "timeline marker submits the original fractional start");
        at(5.4, "chapter marker reaches 5.40 s while paused");
        check(highlighted(2), "marker seek selects the same chapter in both menus");
        undo->trigger();
        at(2, "marker seek can be undone to the original frame time");
    }

    void manualGestures()
    {
        video->setFocus();
        requests.clear();
        QTest::keyPress(video, Qt::Key_Right);
        at(3, "held key first seek settles");
        for (double time : {4., 5.}) {
            QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, {}, true);
            QApplication::sendEvent(video, &repeat);
            at(time, "held key repeat settles in a separate seek");
        }
        QTest::keyRelease(video, Qt::Key_Right);
        check(requests.size() == 3, "three completed seeks came from a single held key");
        requests.clear();
        activate(*control);
        choose(*navigation, {undo});
        requested(2, "held-key undo uses the position before the first press");
        at(2, "one undo reverses every repeat while staying paused");
        check(!undo->isEnabled(), "held-key undo leaves no extra entries");

        activate(main);
        player.setPaused(false);
        check(until([&] { return !player.paused() && player.position() > 2; }), "playback advances before dragging");
        const double beforeDrag = player.position();
        const QPoint end(slider->width() * 3 / 4, slider->height() / 2);
        requests.clear();
        QTest::mousePress(slider, Qt::LeftButton, {}, slider->rect().center());
        QTest::mouseMove(slider, end);
        check(until([&] { return player.position() > beforeDrag + .12; }) && requests.isEmpty(), "a drag previews while playback continues");
        QTest::mouseRelease(slider, Qt::LeftButton, {}, end);
        check(requests.size() == 1 && until([&] { return !player.seekInProgress() && undo->isEnabled(); }), "one drag commits a single seek");
        requests.clear();
        undo->trigger();
        requested(beforeDrag, "drag undo uses the actual position at mouse press");
        check(until([&] { return !player.seekInProgress(); }) && !player.paused() && !undo->isEnabled(), "drag undo preserves playing state and clears the record");
        player.setPaused(true);
        check(until([&] { return player.paused(); }), "paused again for failure checks");

        prime(2);
        player.seek(5.4);
        at(5.4, "successful manual seek establishes an undo record");
        player.seek(5.4);
        at(5.4, "no-displacement seek resolves at the same position");
        requests.clear();
        QTest::mousePress(slider, Qt::LeftButton, {}, end);
        QTest::keyClick(slider, Qt::Key_Escape);
        QTest::mouseRelease(slider, Qt::LeftButton, {}, end);
        check(requests.isEmpty() && undo->isEnabled(), "canceled preview preserves undo without dispatching a seek");

        check(mpv_request_event(player.handle(), MPV_EVENT_PLAYBACK_RESTART, 0) == 0, "simulate missing seek completion feedback");
        player.seek(4);
        check(until([&] {
            double time = 0;
            return player.seekInProgress() && mpv_get_property(player.handle(), "time-pos", MPV_FORMAT_DOUBLE, &time) == 0 && near(time, 4);
        }) && near(player.position(), 5.4) && highlighted(2), "unconfirmed position cannot move chapter highlighting");
        check(until([&] { return !player.seekInProgress(); }, 6500) && undo->isEnabled(), "a timed-out manual seek retains the prior undo record");
        requests.clear();
        undo->trigger();
        requested(2, "failed and no-displacement seeks preserve the prior 2.000000 s origin");
        check(until([&] { return !player.seekInProgress(); }, 6500) && undo->isEnabled(), "an unconfirmed undo remains available for retry");
        check(mpv_request_event(player.handle(), MPV_EVENT_PLAYBACK_RESTART, 1) == 0, "restore completion feedback");
        undo->trigger();
        at(2, "confirmed undo retry returns to the recorded position");
        check(!undo->isEnabled(), "only successful undo clears the record");
    }

    void automaticSeeks()
    {
        prime(2);
        player.setLoopA();
        prime(3);
        player.setLoopB();
        check(player.loopActive(), "A-B loop set at 2 and 3 seconds");
        chapters->actions()[2]->trigger();
        at(5.4, "chapter jump outside A-B succeeds while paused");
        check(!player.loopActive() && until([&] { return player.mediaProperty("ab-loop-a").toString() == "no"
            && player.mediaProperty("ab-loop-b").toString() == "no"; }), "jump outside A-B clears both loop points");
        prime(6);
        QSignalSpy restored(&player, &Player::restored);
        player.setRendererReady(false);
        player.setRendererReady(true);
        check(until([&] { return !restored.isEmpty() && player.canSeek() && chapters->actions().size() == 3; }), "render recovery reloads the same chapters");
        at(6, "render recovery restores the paused position");
        requests.clear();
        undo->trigger();
        requested(3, "automatic start seek and render recovery preserve manual undo");
        at(3, "undo reaches the pre-chapter A-B position");

        auto preferences = session.preferences();
        preferences.skipEnabled = true;
        preferences.introEnabled = preferences.outroEnabled = true;
        preferences.introSeconds = preferences.outroSeconds = 2;
        check(session.applyPreferences(preferences), "enable two-second intro and outro skips");
        playlist->replay();
        check(until([&] { return player.canSeek() && !player.paused() && player.position() >= 2; }) && !undo->isEnabled(), "automatic intro records no manual undo");
        player.setPaused(true);
        check(until([&] { return player.paused(); }), "pause after starting beyond the intro");
        prime(6.4);
        control->findChild<QToolButton *>("controlForwardSmall")->click();
        at(7.4, "small-window seek enters the outro manually");
        player.setPaused(false);
        check(until([&] { return !player.paused() && player.position() >= 7.6; }) && player.itemId() == ids.front(), "manual positioning into the outro allows it to play");
        requests.clear();
        undo->trigger();
        requested(6.4, "undo restores the time before entering the outro");
        check(until([&] { return !player.seekInProgress(); }) && !player.paused() && !undo->isEnabled(), "undo from the outro keeps playback running");
        check(until([&] { return player.itemId() == ids.back() && player.canSeek(); }), "natural return to the outro boundary advances the queue");

        preferences.skipEnabled = false;
        check(session.applyPreferences(preferences), "restore full playback for media-switch checks");
    }

    void switchMedia()
    {
        activate(main);
        video->setFocus();
        QTest::keyClick(video, Qt::Key_PageUp);
        pauseItem(ids.front());
        check(chapters->actions().size() == 3, "viewing-area PageUp selects the previous media item");
        prime(2);
        player.seek(5.4);
        at(5.4, "manual seek before rapid switching creates undo");
        quint64 late = 0;
        const auto switchOnRead = QObject::connect(&player, &Player::requestSubmitted, &main,
            [&](quint64 id, quint64, quint64, const QString &operation) {
                if (!late && operation == "get time-pos") {
                    late = id;
                    playlist->play(ids.back());
                }
            });
        QSignalSpy replies(&player, &Player::requestFinished);
        player.seek(2);
        pauseItem(ids.back());
        QObject::disconnect(switchOnRead);
        bool obsolete = false;
        for (const auto &reply : replies) if (reply[0].toULongLong() == late && !reply[1].toBool()) obsolete = true;
        check(late && obsolete, "old file's asynchronous position confirmation is discarded");
        bool staleChapters = false;
        const auto observeChapters = QObject::connect(&player, &Player::chaptersChanged, &main, [&] {
            if (player.itemId() == ids.back() && !player.chapters().isEmpty()) staleChapters = true;
        });
        playlist->play(ids.front());
        playlist->play(ids.back());
        pauseItem(ids.back());
        QObject::disconnect(observeChapters);
        check(!staleChapters && chapters->actions().isEmpty() && !chapters->menuAction()->isEnabled() && !undo->isEnabled()
            && !actions->get(Id::PreviousChapter)->isEnabled() && !actions->get(Id::NextChapter)->isEnabled(),
            "chapter entries, navigation, markers and undo clear on rapid switch to chapterless media");
        video->setFocus();
        QTest::keyClick(video, Qt::Key_PageUp);
        pauseItem(ids.front());
        video->setFocus();
        QTest::keyClick(video, Qt::Key_PageDown);
        pauseItem(ids.back());
        check(player.chapters().isEmpty(), "viewing-area PageDown selects the next media item");

        playlist->play(ids.front());
        pauseItem(ids.front());
        prime(2);
        player.seek(5.4);
        at(5.4, "prepare chapters and undo before stop");
        actions->get(Id::Stop)->trigger();
        check(player.state() == Player::State::Idle && player.chapters().isEmpty() && chapters->actions().isEmpty()
            && !undo->isEnabled(), "stop immediately clears the chapter list and undo");
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("zopana");
    std::setlocale(LC_NUMERIC, "C");
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    try {
        Probe probe(args[1], args[2]);
        probe.chapterNavigation();
        probe.manualGestures();
        probe.automaticSeeks();
        probe.switchMedia();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
