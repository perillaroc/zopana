#include "mainwindow.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "playlistpanel.h"
#include "folderscanner.h"
#include "videowidget.h"
#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QScreen>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWindow>
#include <qt_windows.h>
#include <cmath>
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
    PlaylistController *queue;
    PlaylistPanel *panel;
    PlaylistHost *host;
    VideoWidget *video;
    mpv_handle *observer = nullptr;
    Player::State state = Player::State::Idle;
    QString media, fixtures, output;
    int loads = 0;
    QJsonArray checks;
    Suite(QString base, QString destination) : window(initialize()), media(QDir::fromNativeSeparators(base)),
        fixtures(QDir::cleanPath(media + "/../zopana-ui-m3-v1")), output(destination)
    {
        QDir().mkpath(output); queue = window.findChild<PlaylistController *>(); panel = window.findChild<PlaylistPanel *>();
        host = window.findChild<PlaylistHost *>(); video = window.findChild<VideoWidget *>();
        observer = mpv_create_client(player.handle(), "m3-test-observer"); player.setVolume(0);
        QObject::connect(&player, &Player::mediaChanged, &window, [this](auto value, const QString &, const QString &) { state = value; if (value == Player::State::Loaded) ++loads; });
        window.show(); window.raise(); window.activateWindow();
        require("renderer ready", until([this] { return video->rendererReady(); })); activate(window);
    }
    Player &initialize() { if (!player.initialize()) throw std::runtime_error("initialization failed"); return player; }
    ~Suite() { player.shutdown(); if (observer) mpv_destroy(observer); }
    QAction *action(const char *name) { return window.findChild<QAction *>(name); }
    void check(QString name, bool passed, QJsonObject fields = {})
    {
        fields["name"] = name; fields["passed"] = passed; checks.append(fields);
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData()); std::fflush(stdout);
    }
    void require(QString name, bool passed) { check(name, passed); if (!passed) throw std::runtime_error(name.toStdString()); }
    void activate(QWidget &widget) { widget.show(); widget.raise(); widget.activateWindow(); require("test window active", until([&] { return QApplication::activeWindow() == &widget; })); }
    double number(const char *name) { double value = -100; mpv_get_property(observer, name, MPV_FORMAT_DOUBLE, &value); return value; }
    bool paused() { int value = 0; mpv_get_property(observer, "pause", MPV_FORMAT_FLAG, &value); return value; }
    void command(const QStringList &arguments)
    {
        std::vector<QByteArray> strings; for (const auto &s : arguments) strings.push_back(s.toUtf8());
        std::vector<const char *> args; for (const auto &s : strings) args.push_back(s.constData()); args.push_back(nullptr);
        require("observer command", mpv_command(observer, args.data()) >= 0);
    }
    void waitLoaded(quint64 id)
    {
        require("latest item loaded", until([&] { return state == Player::State::Loaded && player.itemId() == id; }));
        player.setPaused(true); require("pause confirmed", until([this] { return paused(); })); QTest::qWait(60);
    }
    QList<quint64> replace(const QStringList &names = {"E1.mp4","E2.mp4","E10.mp4"})
    {
        QStringList paths; for (const auto &name : names) paths.append(fixtures + '/' + name);
        const auto ids = queue->addFiles(paths, true); require("queue populated", !ids.isEmpty()); waitLoaded(ids.first()); return ids;
    }
    void eof() { command({"seek", "15.8", "absolute+exact"}); command({"set","speed","8"}); player.setPaused(false); }
    void shot(const QString &name) { check("screenshot " + name, window.grab().save(output + '/' + name + ".png")); }
    void modelCases()
    {
        PlaylistModel model; QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
        const auto ids = model.insert({fixtures + "/E1.mp4", fixtures + "/E2.mp4", fixtures + "/E10.mp4", fixtures + "/E1.mp4"});
        check("canonical paths deduplicate while preserving input order", ids.size() == 3 && model.rowCount() == 3 && model.item(ids[1])->path.endsWith("E2.mp4"));
        model.setCurrent(ids[1]); QPersistentModelIndex selected(model.index(1));
        model.moveRows({}, 1, 1, {}, 3);
        check("row movement preserves stable current and persistent selection", selected.row() == 2 && model.currentId() == ids[1] && selected.data(PlaylistModel::IdRole).toULongLong() == ids[1]);
        auto *mime = model.mimeData({model.index(2)});
        QMimeData transport; transport.setData("application/x-zopana-item", mime->data("application/x-zopana-item"));
        check("single drag survives native MIME transport", model.dropMimeData(&transport, Qt::MoveAction, 0, 0, {}) && model.idAt(0) == ids[1]); delete mime;
        PlaylistModel other; check("foreign playlist drag token is rejected", !other.dropMimeData(&transport, Qt::MoveAction, 0, 0, {}));
        model.moveSelected({ids[1], ids[0]}, 1);
        check("selected adjacent group moves down intact", model.idAt(1) == ids[1] && model.idAt(2) == ids[0]);
        model.moveSelected({ids[1], ids[0]}, -1);
        check("selected group moves up intact", model.idAt(0) == ids[1] && model.idAt(1) == ids[0]);
        model.remove({ids[1]}); check("remove current clears identity without renumbering items", model.currentId() == 0 && model.item(ids[0]));
        const auto maximum = ids.last(); model.clear(); const auto next = model.insert({fixtures + "/E1.mp4"});
        check("item IDs are not reused after clearing", next.first() > maximum);
        check("duration is not fabricated", !model.data(model.index(0), Qt::DisplayRole).toString().contains("00:"));
    }
    void ingress()
    {
        auto ids = replace(); const auto current = player.itemId(); const int before = loads;
        queue->addFiles({fixtures + "/E1.mp4", fixtures + "/bad1.mkv", fixtures + "/ignore.txt"}, false, 1);
        check("append inserts new files without replacing current", queue->model().rowCount() == 4 && queue->model().idAt(1) != ids[1] && player.itemId() == current && loads == before && paused());
        QSignalSpy messages(queue, &PlaylistController::message);
        queue->addFiles({fixtures + "/ignore.txt"}, true);
        check("invalid replacement keeps current queue and explains result", queue->model().rowCount() == 4 && player.itemId() == current && !messages.isEmpty());
        bool chooser = false;
        QTimer::singleShot(100, &window, [&] { auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); if (dialog) { chooser = dialog->fileMode() == QFileDialog::ExistingFiles; dialog->reject(); } });
        activate(window); video->setFocus(); QTest::keyClick(video, Qt::Key_O, Qt::ControlModifier);
        check("multi-select chooser cancellation changes nothing", chooser && loads == before && queue->model().rowCount() == 4);
        QTimer::singleShot(100, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            auto *edit = dialog->findChild<QLineEdit *>("fileNameEdit");
            edit->setText('"' + fixtures + "/E1.mp4\" \"" + fixtures + "/E2.mp4\""); QTest::keyClick(edit, Qt::Key_Return);
        });
        video->setFocus(); QTest::keyClick(video, Qt::Key_O, Qt::ControlModifier);
        require("file chooser imports both selected files", queue->model().rowCount() == 2); waitLoaded(queue->model().idAt(0));
        auto drop = [&](QWidget *target, const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QMimeData mime; mime.setUrls(urls); const auto point = target->rect().center();
            QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, modifiers); QApplication::sendEvent(target, &enter);
            QDropEvent event(point, Qt::CopyAction, &mime, Qt::LeftButton, modifiers); QApplication::sendEvent(target, &event);
        };
        drop(&window, {QUrl::fromLocalFile(fixtures + "/E1.mp4"), QUrl::fromLocalFile(fixtures + "/E2.mp4")});
        require("multi-file video drop replaces all items", queue->model().rowCount() == 2); waitLoaded(queue->model().idAt(0));
        const int after = loads;
        drop(&window, {QUrl::fromLocalFile(fixtures + "/E10.mp4")}, Qt::ControlModifier);
        check("Ctrl drop appends and keeps playback", queue->model().rowCount() == 3 && loads == after && paused());
        drop(panel->view()->viewport(), {QUrl::fromLocalFile(fixtures + "/bad2.mkv")});
        check("playlist drop appends to same model", queue->model().rowCount() == 4 && loads == after);
        {
            QMimeData mime; mime.setUrls({QUrl::fromLocalFile(fixtures + "/bad3.mkv")});
            const QPoint position = panel->view()->visualRect(queue->model().index(0)).topLeft() + QPoint(3, 2);
            QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(panel->view()->viewport(), &enter);
            QDragMoveEvent move(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(panel->view()->viewport(), &move);
            check("external drop shows explicit insertion row", panel->view()->property("externalDropRow").toInt() == 0);
            QDropEvent event(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(panel->view()->viewport(), &event);
            check("external insertion keeps active item and inserts at top", queue->model().item(queue->model().idAt(0))->path.endsWith("bad3.mkv") && loads == after);
        }
        QSignalSpy subtitles(&player, &Player::subtitleLoaded);
        drop(&window, {QUrl::fromLocalFile(fixtures + "/E1.mp4"), QUrl::fromLocalFile(fixtures + "/E1.srt")});
        require("matching subtitle follows mixed media drop", until([&] { return subtitles.count() == 1; }));
        check("mixed drop has one authoritative media item", queue->model().rowCount() == 1 && queue->model().item(queue->model().currentId())->subtitles.size() == 1);
    }
    void scanner()
    {
        FolderScanner scanner; QSignalSpy results(&scanner, &FolderScanner::completed);
        int ticks = 0; QTimer timer; timer.setInterval(1); QObject::connect(&timer, &QTimer::timeout, &window, [&] { ++ticks; }); timer.start();
        scanner.start(fixtures); require("async nonrecursive scan finishes", until([&] { return !results.isEmpty(); }));
        const auto files = results.first()[0].toStringList();
        check("scan naturally sorts E1 E2 E10 and excludes subdirectories", files.size() == 6 && QFileInfo(files[3]).fileName() == "E1.mp4" && QFileInfo(files[4]).fileName() == "E2.mp4" && QFileInfo(files[5]).fileName() == "E10.mp4");
        check("scan reports invalid nonmedia files", results.first()[1].toInt() >= 2);
        results.clear(); scanner.start(fixtures + "/large"); scanner.cancel(); QTest::qWait(100);
        check("cancel discards results without queue mutation", results.isEmpty() && !scanner.busy());
        scanner.start(fixtures + "/large"); require("1000-file scan finishes", until([&] { return !results.isEmpty(); }));
        const auto large = results.first()[0].toStringList();
        check("1000-file scan keeps UI event loop responsive", large.size() == 1000 && ticks > 0);
        QElapsedTimer elapsed; elapsed.start(); queue->addFiles(large, true); const auto id = queue->model().idAt(0); waitLoaded(id);
        host->setVisible(true, false); panel->select(queue->model().idAt(900));
        QTest::qWait(60); shot("playlist-1000");
        check("1000-item view is usable without metadata probing", queue->model().rowCount() == 1000 && elapsed.elapsed() < 6000 && panel->selectedId() == queue->model().idAt(900));
        const int scroll = panel->view()->verticalScrollBar()->value();
        host->setDetached(true); QTest::qWait(60); host->setDetached(false); QTest::qWait(60);
        check("host switch preserves nonzero scroll and independent selection", scroll > 0 && panel->view()->verticalScrollBar()->value() == scroll && panel->selectedId() == queue->model().idAt(900));
        const auto selected = panel->selectedId(), current = queue->model().currentId(); const int before = loads;
        player.setRendererReady(false); player.setRendererReady(true); waitLoaded(current);
        check("renderer reload retains current identity selection and queue", loads == before + 1 && queue->model().rowCount() == 1000 && panel->selectedId() == selected && queue->model().currentId() == current);
    }
    void transitions()
    {
        auto ids = replace(); player.seek(6); queue->play(ids[1]); queue->play(ids[2]); waitLoaded(ids[2]);
        check("rapid A B C ends only on C at its own start", queue->model().currentId() == ids[2] && number("time-pos") < 1);
        check("sequential boundary actions reflect position", action("play.previous")->isEnabled() && !action("play.next")->isEnabled());
        command({"set","ab-loop-a","2"}); command({"set","ab-loop-b","3"}); queue->previous(); waitLoaded(ids[1]);
        char *a = mpv_get_property_string(observer, "ab-loop-a"), *b = mpv_get_property_string(observer, "ab-loop-b");
        check("manual item change clears AB loop", a && b && QByteArray(a) == "no" && QByteArray(b) == "no"); mpv_free(a); mpv_free(b);
        queue->play(ids[2]); waitLoaded(ids[2]);
        const int before = loads; panel->select(ids[0]);
        check("selection alone never changes playing item", player.itemId() == ids[2] && loads == before);
        queue->remove({ids[0]}); check("removing noncurrent keeps playback", player.itemId() == ids[2] && paused());
        queue->remove({ids[2]}); QTest::qWait(150);
        check("removing current stops and selects remaining item without autoplay", state == Player::State::Idle && queue->model().currentId() == 0 && panel->selectedId() == ids[1] && loads == before);
        action("queue.playSelected")->trigger(); waitLoaded(ids[1]);
        queue->clear(); check("clear stops without deleting files", queue->model().rowCount() == 0 && state == Player::State::Idle && QFile::exists(fixtures + "/E2.mp4"));
        ids = replace(); queue->setPlaybackOrder(RepeatMode::One, false); const int baseline = loads;
        eof(); require("single repeat reopens same item", until([&] { return loads > baseline && player.itemId() == ids[0]; }));
        queue->next(); waitLoaded(ids[1]); check("manual next ignores single repeat", queue->model().currentId() == ids[1]);
        queue->setPlaybackOrder(RepeatMode::All, false); queue->play(ids.last()); waitLoaded(ids.last());
        check("order actions reflect authoritative order", action("queue.order.list")->isChecked() && !action("queue.order.single")->isChecked());
        eof(); require("list repeat wraps from tail", until([&] { return player.itemId() == ids.first() && state == Player::State::Loaded; }));
        queue->setPlaybackOrder(RepeatMode::None, false); queue->play(ids.last()); waitLoaded(ids.last());
        eof(); require("sequential EOF at tail stops", until([&] { return state == Player::State::Ended; })); const int ended = loads; QTest::qWait(120);
        check("sequential tail does not auto replay", loads == ended);
        command({"set","speed","1"});
        ids = replace(); queue->setPlaybackOrder(RepeatMode::All, true); QSet<quint64> round{ids[0]};
        for (int i = 0; i < 2; ++i) { queue->next(); waitLoaded(queue->model().currentId()); round.insert(queue->model().currentId()); }
        check("random visits each item once per round", round.size() == 3);
        const auto last = queue->model().currentId(); queue->previous(); const auto previous = queue->model().currentId(); waitLoaded(previous);
        queue->next(); waitLoaded(last); check("random previous and forward follow playback history", queue->model().currentId() == last && previous != last);
        queue->next(); waitLoaded(queue->model().currentId()); check("random new round avoids adjacent duplicate", queue->model().currentId() != last);
        const auto generation = player.generation(), current = player.itemId();
        player.sessionChanged(generation - 1, current, Player::State::Ended, {}); QTest::qWait(100);
        check("stale generation EOF cannot advance", player.itemId() == current && player.generation() == generation);
        queue->setPlaybackOrder(RepeatMode::All, false); ids = replace({"E1.mp4","bad1.mkv","bad2.mkv","bad3.mkv"});
        int failures = 0; const auto good = ids.first();
        const auto connection = QObject::connect(&player, &Player::sessionChanged, &window, [&](quint64, quint64 id, Player::State value, const QString &) {
            if (id == ids[1] && value == Player::State::Loading) queue->remove({good});
            if (value == Player::State::Error) ++failures;
        });
        eof(); require("all-bad loop stops after bounded traversal", until([&] { return failures == 3 && state == Player::State::Error; }));
        QTest::qWait(200); check("all-bad queue never spins", failures == 3 && queue->model().rowCount() == 3);
        QObject::disconnect(connection); queue->setPlaybackOrder(RepeatMode::None, false); command({"set","speed","1"});
        queue->addFiles({fixtures + "/bad1.mkv", fixtures + "/bad2.mkv"}, true);
        require("manual bad file stays at error", until([&] { return state == Player::State::Error; }));
        const auto bad = player.itemId(); QTest::qWait(100);
        check("manual failure does not automatically skip", player.itemId() == bad && queue->model().currentId() == bad);
    }
    void windows()
    {
        auto ids = replace(); host->setVisible(true, false); QTest::qWait(60);
        auto *view = panel->view(); const auto context = video->context(); const int before = loads;
        panel->select(ids[1]); activate(window); view->setFocus(); QTest::keyClick(view, Qt::Key_Return); waitLoaded(ids[1]);
        check("queue Enter plays selected without fullscreen", !window.isFullScreen());
        QSignalSpy toggles(action("play.toggle"), &QAction::triggered); view->setFocus(); QTest::keyClick(view, Qt::Key_Space);
        check("queue Space does not toggle playback", toggles.isEmpty() && paused());
        view->setFocus(); QTest::keyClick(view, Qt::Key_Up, Qt::ControlModifier);
        check("queue Ctrl Up moves selected without seeking", queue->model().idAt(0) == ids[1] && number("time-pos") < 1);
        QInputMethodEvent composing(QString::fromUtf8("中"), {}); QApplication::sendEvent(view, &composing);
        const auto first = queue->model().idAt(0); const int oldLoads = loads;
        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier); QTest::keyClick(view, Qt::Key_Return);
        check("IME composition suppresses queue playback and reordering", queue->model().idAt(0) == first && loads == oldLoads);
        QInputMethodEvent finished; QApplication::sendEvent(view, &finished);
        const int stableLoads = loads;
        for (int i = 0; i < 20; ++i) { host->setDetached(true); host->setDetached(false); QTest::qWait(10); }
        check("20 host switches retain model selection playback and GL context", loads == stableLoads && panel->selectedId() == ids[1] && player.itemId() == ids[1] && video->context() == context && paused());
        host->setDetached(true); activate(*host->window()); view->setFocus(); QTest::qWait(80);
        const auto mainHandle = reinterpret_cast<HWND>(window.winId());
        const auto listHandle = reinterpret_cast<HWND>(host->window()->winId());
        check("floating playlist is owned and excluded from the Windows taskbar", host->window()->windowHandle()->transientParent() == window.windowHandle()
            && GetWindow(listHandle, GW_OWNER) == mainHandle && (GetWindowLongPtrW(listHandle, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
            && !(GetWindowLongPtrW(listHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW));
        check("independent list keeps shared model", view->model() == &queue->model());
        host->window()->move(host->window()->pos() + QPoint(30, 20)); host->window()->resize(350, 530); const auto geometry = host->window()->geometry();
        window.move(window.pos() + QPoint(15, 10)); QTest::qWait(30); check("moving main does not move independent list", host->window()->geometry() == geometry);
        auto *toggle = window.findChild<QToolButton *>("playlistButton");
        activate(window);
        QTest::mouseClick(toggle, Qt::LeftButton);
        check("one main-window click hides an inactive floating list", !host->visible() && !host->window()->isVisible() && !toggle->isChecked());
        QTest::mouseClick(toggle, Qt::LeftButton);
        check("one click restores floating list and selection", host->window()->isVisible() && toggle->isChecked() && panel->selectedId() == ids[1]);
        window.showMinimized(); QTest::qWait(100);
        check("main minimization hides floating list while preserving preference", host->preferredVisible() && !IsWindowVisible(listHandle));
        window.showNormal(); activate(window);
        check("main restore shows floating list at the same position", until([&] { return IsWindowVisible(listHandle) && host->window()->geometry() == geometry; }));

        auto *back = window.findChild<QToolButton *>("backButton");
        auto *forward = window.findChild<QToolButton *>("forwardButton");
        require("direction buttons ready for seeking", until([&] { return action("seek.forwardSmall")->isEnabled(); }));
        player.seek(7);
        require("seek fixture starts at seven seconds", until([&] { return std::abs(number("time-pos") - 7) < .1; }));
        QTest::mouseClick(back, Qt::LeftButton);
        check("left click on back seeks five seconds in current item", until([&] { return std::abs(number("time-pos") - 2) < .1 && player.itemId() == ids[1]; }));
        QTest::mouseClick(forward, Qt::LeftButton);
        check("left click on forward seeks five seconds in current item", until([&] { return std::abs(number("time-pos") - 7) < .1 && player.itemId() == ids[1]; }));
        QTest::mouseClick(forward, Qt::RightButton); waitLoaded(ids[0]);
        QTest::mouseClick(back, Qt::RightButton); waitLoaded(ids[1]);
        check("right clicks skip playlist items without opening a menu", !QApplication::activePopupWidget());
        check("direction icons remain visible after media changes", !back->icon().isNull() && !forward->icon().isNull());
        activate(*host->window()); view->setFocus(); QTest::keyClick(view, Qt::Key_W, Qt::ControlModifier);
        check("Ctrl W hides only independent list", until([&] { return !host->visible(); }) && state == Player::State::Loaded);
        host->setVisible(true); QTest::qWait(40); check("reshow preserves independent geometry", host->window()->geometry() == geometry && host->detached());
        host->window()->close(); check("native close hides without destroying queue", !host->visible() && queue->model().rowCount() == 3);
        host->setVisible(true); window.showNormal(); QTest::qWait(50); activate(*host->window()); view->setFocus(); QTest::keyClick(view, Qt::Key_F11); QTest::qWait(80);
        check("independent F11 controls main fullscreen", window.isFullScreen() && host->window()->isVisible());
        activate(*host->window()); view->setFocus(); QTest::keyClick(view, Qt::Key_Escape); check("independent Escape leaves main fullscreen", window.isFullScreen() && host->visible());
        bool modalSafe = false;
        QTimer::singleShot(80, &window, [&] {
            auto *dialog = QApplication::activeModalWidget(); if (!dialog) return;
            QTest::keyClick(view, Qt::Key_F11); QTest::keyClick(view, Qt::Key_Return);
            modalSafe = window.isFullScreen() && player.itemId() == ids[1]; QTest::keyClick(dialog, Qt::Key_Escape);
        });
        action("window.shortcuts")->trigger(); check("modal dialog protects both windows", modalSafe);
        activate(window); video->setFocus(); QTest::keyClick(video, Qt::Key_Escape); QTest::qWait(80);
        host->setDetached(false); host->setVisible(true, false);
        for (auto size : {QSize(1120,720),QSize(800,500),QSize(640,400)}) {
            window.resize(size); QTest::qWait(80);
            check(QString("playlist layout %1 fits window").arg(size.width()), window.size() == size && panel->width() >= 260);
            if (size.width() < 900) {
                auto *overlay = window.findChild<QWidget *>("playlistOverlay");
                auto *topBar = window.findChild<QWidget *>("playerTopBar");
                auto *controls = window.findChild<QWidget *>("controlBar");
                check("narrow overlay leaves both control bars usable", overlay->y() >= topBar->geometry().bottom() + 1
                    && overlay->geometry().bottom() < controls->mapTo(window.centralWidget(), QPoint()).y());
                const auto geometry = video->geometry(); host->setVisible(false, false); QTest::qWait(40);
                check("narrow overlay hide preserves viewport", video->geometry() == geometry); host->setVisible(true, false);
                activate(window); view->setFocus(); QTest::keyClick(view, Qt::Key_Escape); check("Escape closes overlay one layer", !host->overlayVisible() && !window.isFullScreen()); host->setVisible(true, false);
            }
            shot(QString("docked-%1").arg(size.width()));
        }
        window.resize(1120,720); host->setVisible(true, false); activate(window); video->setFocus(); QTest::keyClick(video, Qt::Key_F11); QTest::qWait(80);
        check("fullscreen initially collapses docked list", !host->visible()); QTest::keyClick(video, Qt::Key_P); check("P opens fullscreen playlist overlay", host->overlayVisible());
        QTest::keyClick(panel->view(), Qt::Key_Escape); check("first Escape dismisses overlay", window.isFullScreen() && !host->visible());
        video->setFocus(); QTest::keyClick(video, Qt::Key_Escape); QTest::qWait(80); check("fullscreen exit restores dock preference", host->visible() && !window.isFullScreen());
        host->setDetached(true); host->setVisible(true); QTest::qWait(50);
        check("detached screenshot", host->window()->grab().save(output + "/detached.png"));
        if (QGuiApplication::screens().size() > 1) { auto *screen = QGuiApplication::screens()[1]; host->window()->move(screen->availableGeometry().center() - host->window()->rect().center()); QTest::qWait(80); check("second-screen list stays usable", screen->availableGeometry().intersects(host->window()->geometry())); }
        check("host switches did not cause unrelated loads", loads == before + 3, {{"before",before},{"after",loads}});
        window.setWindowFlag(Qt::WindowStaysOnTopHint, true); window.show(); QTest::qWait(80);
        check("main topmost flag does not propagate to playlist", !(host->window()->windowFlags() & Qt::WindowStaysOnTopHint));
        window.setWindowFlag(Qt::WindowStaysOnTopHint, false); window.show(); QTest::qWait(80);
        window.close(); check("main close hides independent window", !host->window()->isVisible());
    }
    void run() { modelCases(); ingress(); scanner(); transitions(); windows(); }
    bool save()
    {
        bool passed = true; for (const auto &value : checks) passed &= value.toObject()["passed"].toBool();
        QFile file(output + "/results.json"); if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(QJsonObject{{"passed",passed},{"devicePixelRatio",video->devicePixelRatioF()},{"screens",QGuiApplication::screens().size()},{"checks",checks}}).toJson()); return passed;
    }
};
}
int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QApplication app(argc,argv); app.setApplicationName("zopana"); app.setQuitOnLastWindowClosed(false);
    if (app.arguments().size() == 4 && app.arguments()[1] == "--lifecycle") {
        Suite suite(app.arguments()[2], app.arguments()[3]); app.setQuitOnLastWindowClosed(true);
        suite.host->setDetached(true); suite.host->setVisible(true);
        QTimer::singleShot(100, &suite.window, [&] { suite.window.close(); });
        QTimer::singleShot(3000, &app, [&] { app.exit(3); });
        const int result = app.exec(); suite.check("closing main exits application with independent window present", result == 0 && !suite.host->window()->isVisible());
        return suite.save() ? 0 : 1;
    }
    if (app.arguments().size()!=3) return 2;
    try { Suite suite(app.arguments()[1],app.arguments()[2]); try { suite.run(); } catch (const std::exception &error) { suite.check("aborted",false,{{"reason",error.what()}}); } return suite.save()?0:1; }
    catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 2; }
}
