#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlistfile.h"
#include "playlisthost.h"
#include "playlistpanel.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <windows.h>

namespace {
using Id = PlayerActions::Id;
constexpr auto workerLog = "ZOPANA_PLAYLIST_IO_WORKER_LOG";

void require(bool condition, const char *label)
{
    if (!condition) throw std::runtime_error(label);
}

void check(bool condition, const char *label)
{
    require(condition, label);
    std::printf("PASS %s\n", label);
    std::fflush(stdout);
}

bool until(auto condition, int timeout = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void write(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size(), "write fixture");
}

Player &initialize(Player &player)
{
    require(player.initialize(), "initialize libmpv");
    return player;
}

struct Probe {
    QDir media, output;
    SettingsStore settings{output.filePath("config")};
    HistoryStore history{output.filePath("data")};
    Player player;
    MainWindow main{initialize(player)};
    SessionController session{player, main, settings, history};
    PlaylistController &queue = *main.findChild<PlaylistController *>();
    PlaylistModel &model = queue.model();
    PlaylistPanel &panel = *main.findChild<PlaylistPanel *>();
    PlaylistHost &host = *main.findChild<PlaylistHost *>();
    PlayerActions &actions = *main.findChild<PlayerActions *>();
    QListView *view = panel.view();
    QLineEdit *search = panel.findChild<QLineEdit *>("playlistSearch");
    VideoWidget *video = main.findChild<VideoWidget *>("videoWidget");
    QSignalSpy opens{&player, &Player::fileOpening};
    QSignalSpy messages{&queue, &PlaylistController::message};

    Probe(const QString &fixtures, const QString &evidence) : media(fixtures), output(evidence)
    {
        main.resize(1100, 700);
        main.show();
        activate(main);
        check(until([&] { return video->rendererReady(); }), "real Qt player window and renderer ready");
        std::printf("Qt %s, DPR %.2f\n", qVersion(), video->devicePixelRatioF());
        player.setMuted(true);
    }

    void activate(QWidget &window)
    {
        window.raise(); window.activateWindow();
        require(QTest::qWaitForWindowActive(&window), "activate probe window");
    }

    QString copy(const QString &name, const QString &source = "E1.mp4")
    {
        const auto path = output.filePath(name);
        require(QDir().mkpath(QFileInfo(path).absolutePath()) && QFile::copy(media.filePath(source), path), "copy probe fixture");
        return path;
    }

    QAction *action(Id id) const { return actions.get(id); }

    QStringList paths() const
    {
        QStringList result;
        for (int row = 0; row < model.rowCount(); ++row) result.append(model.index(row).data(PlaylistModel::PathRole).toString());
        return result;
    }

    QList<quint64> visibleIds() const
    {
        QList<quint64> result;
        for (int row = 0; row < view->model()->rowCount(); ++row) result.append(view->model()->index(row, 0).data(PlaylistModel::IdRole).toULongLong());
        return result;
    }

    void pause(quint64 id)
    {
        check(until([&] { return player.itemId() == id && player.canSeek() && video->canCapture(); }), "expected item reaches seekable playback");
        player.setPaused(true);
        require(until([&] { return player.paused(); }), "pause confirmed");
    }

    void seek(double target)
    {
        player.seek(target);
        require(until([&] { return !player.seekInProgress() && std::abs(player.position() - target) < .06; }), "seek fixture position");
    }

    void choose(Id id, const QString &path = {})
    {
        bool handled = false, accepted = false;
        QTimer::singleShot(80, &main, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            handled = true;
            if (path.isEmpty()) {
                dialog->reject();
                return;
            }
            const QFileInfo file(path);
            dialog->setDirectory(file.absolutePath());
            auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
            if (!name) { dialog->reject(); return; }
            name->setText(path);
            QObject::connect(dialog, &QDialog::accepted, dialog, [&] { accepted = true; });
            QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            if (dialog->isVisible()) dialog->reject();
        });
        action(id)->trigger();
        check(handled && accepted == !path.isEmpty(), "playlist action opens the file picker and handles the chosen result");
    }

    void drop(const QString &path, const QPoint &point)
    {
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(point, Qt::CopyAction, &data, Qt::LeftButton, {});
        QDragMoveEvent move(point, Qt::CopyAction, &data, Qt::LeftButton, {});
        QDropEvent event(point, Qt::CopyAction, &data, Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &enter);
        QApplication::sendEvent(view->viewport(), &move);
        QApplication::sendEvent(view->viewport(), &event);
        check(enter.isAccepted() && move.isAccepted() && event.isAccepted(), "playlist accepts the external file drop");
    }

    void inputMethod()
    {
        search->clear();
        search->setFocus();
        require(search->hasFocus(), "search focus");
        const auto identity = player.playbackIdentity();
        const bool fullScreen = main.isFullScreen();
        const int before = int(opens.size());
        QInputMethodEvent preedit("tong ming", {});
        QApplication::sendEvent(search, &preedit);
        QTest::keyClick(search, Qt::Key_Space);
        QTest::keyClick(search, Qt::Key_Return);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("同名"));
        QApplication::sendEvent(search, &commit);
        check(search->text().contains(QStringLiteral("同名")) && opens.size() == before && player.playbackIdentity() == identity
            && player.paused() && main.isFullScreen() == fullScreen, "IME preedit Space and Return stay in the focused search input");
    }

    void searchLargeQueue()
    {
        const auto firstCopy = copy(QStringLiteral("media/first/同名 样片.mp4"));
        const auto secondCopy = copy(QStringLiteral("media/second/同名 样片.mp4"), "E2.mp4");
        QDir large(media.filePath("large"));
        QStringList paths{media.filePath("E1.mp4")};
        for (const auto &name : large.entryList({"*.mp4"}, QDir::Files, QDir::Name)) paths.append(large.filePath(name));
        check(paths.size() == 1001, "read the thousand-item fixture bank");
        const auto ids = queue.addFiles(paths, true);
        check(ids.size() == paths.size(), "full source queue is populated");
        pause(ids.front());
        host.setDetached(false);
        host.setVisible(true);
        const auto marker = output.filePath("metadata-worker.log");
        qputenv(workerLog, QFile::encodeName(marker));
        auto pair = queue.addFiles({firstCopy, secondCopy}, false);
        require(pair.size() == 2, "insert the same-name pair");
        panel.select(ids[900]);
        search->setText("EPISODE-0900");
        check(visibleIds().size() == 1, "file-name search ignores case");
        search->setText("MEDIA\\SECOND\\");
        check(visibleIds() == QList{pair[1]}, "search accepts Windows separators in the path");
        search->setText(QStringLiteral("独有缓存标题"));
        check(visibleIds().isEmpty(), "uncached title results are empty");
        model.setMetadata(pair[1], {}, PlaylistMetadata{PlaylistMetadata::State::Read, QStringLiteral("独有缓存标题")});
        check(visibleIds() == QList{pair[1]}, "an arriving cached title updates matches");
        search->clear();
        check(panel.selectedId() == ids[900] && view->model()->rowCount() == model.rowCount(), "clearing a series of queries restores the previously selected item ID");

        for (const auto mode : {"playlistDock", "playlistOverlay", "playlistWindow"}) {
            const QByteArray hostName(mode);
            if (hostName == "playlistOverlay") main.resize(780, 630);
            if (hostName == "playlistWindow") host.setDetached(true);
            check(until([&] { return panel.parentWidget()->objectName() == QLatin1String(mode) && view->isVisible(); }), mode);
            activate(*panel.window());
            if (player.state() != Player::State::Loaded) { queue.play(ids.front()); pause(ids.front()); }
            panel.select(ids[900]);
            inputMethod();
            search->setText(QStringLiteral("同名 样片"));
            check(visibleIds() == pair && !view->dragEnabled(), "search distinguishes same-name files by their two item IDs");
            panel.select(pair[1]);
            view->setFocus();
            QTest::qWait(40);
            const auto point = view->visualRect(view->currentIndex()).center();
            QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, point);
            QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, point);
            pause(pair[1]);
            panel.select(pair[0]);
            QTest::keyClick(view, Qt::Key_Return);
            pause(pair[0]);

            auto *viewModel = view->model();
            const auto before = this->paths();
            std::unique_ptr<QMimeData> drag(viewModel->mimeData({viewModel->index(0, 0)}));
            QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier);
            check(!action(Id::QueueUp)->isEnabled() && !action(Id::QueueDown)->isEnabled()
                && !viewModel->moveRow({}, 0, {}, 2) && !viewModel->dropMimeData(drag.get(), Qt::MoveAction, 2, 0, {})
                && before == this->paths(), "filtered view blocks internal drag and reorder keys");
            view->selectionModel()->select(viewModel->index(1, 0), QItemSelectionModel::Select);
            search->setText("first/");
            check(panel.selectedIds() == QList{pair[0]}, "only the visible portion of a multi-selection is actionable");
            search->clear();
            const auto selected = panel.selectedIds();
            check(selected.size() == 2 && selected.contains(pair[0]) && selected.contains(pair[1]) && panel.selectedId() == pair[0],
                "clearing restores both selected IDs and the current item");
            search->setText(QStringLiteral("同名 样片"));
            view->setFocus();
            QTest::keyClick(view, Qt::Key_Delete);
            check(!model.item(pair[0]) && !model.item(pair[1]) && model.item(ids[900]) && model.rowCount() == ids.size(), "Delete removes only the selected visible source items");
            search->clear();
            pair = queue.addFiles({firstCopy, secondCopy}, false);
        }

        const auto atEnd = copy(QStringLiteral("media/末尾追加.mp4"));
        const auto atStart = copy(QStringLiteral("media/落点插入.mp4"));
        search->setText("no matches for drop");
        check(view->model()->rowCount() == 0, "filter can hide every source row");
        drop(atEnd, QPoint(12, 12));
        check(this->paths().last() == atEnd && view->model()->rowCount() == 0, "external drop appends to the complete queue while filtering");
        search->clear();
        view->scrollToTop();
        QTest::qWait(40);
        drop(atStart, view->visualRect(view->model()->index(0, 0)).topLeft() + QPoint(4, 2));
        check(this->paths().first() == atStart && view->dragEnabled(), "clearing restores drop insertion at the visible position");
        const auto inserted = model.idAt(0);
        panel.select(inserted);
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier);
        check(model.idAt(1) == inserted && view->model()->moveRow({}, 1, {}, 0) && model.idAt(0) == inserted, "clearing restores reorder shortcuts and the view model's row moves");
        QTest::qWait(150);
        check(!QFile::exists(marker), "searching the 1000-item queue started zero metadata workers");
        qunsetenv(workerLog);
        host.setDetached(false);
        activate(main);
    }

    void playbackWithFilter()
    {
        const auto ids = queue.addFiles({media.filePath("E1.mp4"), media.filePath("E2.mp4"), media.filePath("E10.mp4")}, true);
        pause(ids[0]);
        search->setText("E10.mp4");
        check(visibleIds() == QList{ids[2]}, "the current item is hidden by the query");
        seek(player.lastPosition() - .2);
        player.setPaused(false);
        check(until([&] { return player.itemId() == ids[1] && player.state() == Player::State::Loaded; }), "natural playback advances to the hidden next source item");
        queue.setPlaybackOrder(RepeatMode::None, true);
        queue.play(ids[0]);
        pause(ids[0]);
        search->setText("no random results");
        seek(player.lastPosition() - .2);
        player.setPaused(false);
        check(until([&] { return player.state() == Player::State::Loaded && (player.itemId() == ids[1] || player.itemId() == ids[2]); })
            && visibleIds().isEmpty(), "shuffle advances from the full queue while all candidates are hidden");
        queue.setPlaybackOrder(RepeatMode::None, false);
        search->clear();
    }

    QString roundTripPath() const { return output.filePath(QStringLiteral("lists/往返 片单.m3u8")); }
    QStringList roundTripOrder() const
    {
        return {output.filePath(QStringLiteral("media/#追加 C.mp4")), output.filePath(QStringLiteral("media/第二段 B.mp4")),
            output.filePath(QStringLiteral("media/开场 A.mp4"))};
    }

    void playlistFiles()
    {
        const auto a = copy(QStringLiteral("media/开场 A.mp4"));
        const auto b = copy(QStringLiteral("media/第二段 B.mp4"), "E2.mp4");
        const auto c = copy(QStringLiteral("media/#追加 C.mp4"), "E10.mp4");
        const auto list = output.filePath("lists/open.m3u8");
        require(QDir().mkpath(QFileInfo(list).absolutePath()), "make playlist fixture directory");
        write(output.filePath("media/note.txt"), "neutral fixture");
        const auto mixed = QStringLiteral("#EXTM3U\r\n\r\n# a comment\r\n#EXTINF:5,title\r\n..\\media\\开场 A.mp4\r\n%1\r\n"
            "../media/./开场 A.mp4\r\n../media/missing file.mp4\r\n../media/note.txt\r\nZ:incomplete.mp4\r\n"
            "https://example.invalid/video.mp4\r\n\\\\example.invalid\\share\\video.mp4\r\n").arg(QDir::toNativeSeparators(b));
        write(list, QByteArray("\xEF\xBB\xBF") + mixed.toUtf8());
        auto *fileMenu = main.findChild<QMenu *>("fileMenu");
        auto *context = panel.findChild<QMenu *>("playlistContextMenu");
        for (auto id : {Id::OpenPlaylistFile, Id::AppendPlaylistFile, Id::ExportPlaylistFile})
            check(fileMenu->actions().contains(action(id)) && context->actions().contains(action(id)), "file and playlist context menus share each playlist command");
        opens.clear();
        messages.clear();
        choose(Id::OpenPlaylistFile, list);
        check(paths() == QStringList{a, b} && opens.size() == 1, "open validates, deduplicates and replaces the queue with one first-item start");
        const auto skipped = messages.last().front().toString();
        check(skipped.contains(QStringLiteral("2 个网络")) && skipped.contains(QStringLiteral("1 个无效"))
            && skipped.contains(QStringLiteral("1 个缺失")) && skipped.contains(QStringLiteral("1 个格式"))
            && skipped.contains(QStringLiteral("1 个重复")), "open summarizes remote, invalid, unreadable, unsupported and duplicate entries");
        pause(model.idAt(0));
        seek(2.4);
        QTest::qWait(80);
        const auto held = player.playbackIdentity();
        const auto position = player.position();
        const auto frame = video->grabFramebuffer();
        const auto intact = [&] { return player.playbackIdentity() == held && player.position() == position && player.paused() && opens.size() == 1; };

        choose(Id::OpenPlaylistFile);
        choose(Id::AppendPlaylistFile);
        check(paths() == QStringList{a, b} && intact(), "canceling either file picker preserves queue and paused playback");
        check(queue.importPlaylist(output.filePath("lists/unreadable.m3u8"), true).isEmpty() && paths() == QStringList{a, b} && intact(), "unreadable playlist preserves the existing queue");
        const auto bad = output.filePath("lists/bad.m3u8");
        write(bad, "../media/missing.mp4\nhttps://example.invalid/stream\n");
        choose(Id::OpenPlaylistFile, bad);
        check(paths() == QStringList{a, b} && intact(), "a fully invalid list does not replace the queue");
        write(bad, b.toUtf8() + "\n#EXT-X-TARGETDURATION:6\n");
        check(queue.importPlaylist(bad, true).isEmpty() && messages.last().front().toString().contains("HLS")
            && intact(), "an HLS tag after a valid entry rejects the entire file");
        write(bad, b.toUtf8() + '\n' + QByteArray("\xE4\xB8", 2));
        check(queue.importPlaylist(bad, true).isEmpty() && messages.last().front().toString().contains("UTF-8")
            && intact(), "incomplete UTF-8 rejects the entire file");
        choose(Id::AppendPlaylistFile, list);
        check(paths() == QStringList{a, b} && intact(), "append with only duplicate media keeps the paused session and queue");
        const auto append = output.filePath("lists/append.m3u8");
        write(append, QStringLiteral("../media/开场 A.mp4\n../media/missing file.mp4\n../media/#追加 C.mp4\n").toUtf8());
        const auto parsed = PlaylistFile::read(append);
        check(parsed.error.isEmpty() && parsed.paths.size() == 3 && parsed.paths.last() == c, "mixed append resolves the new path with a literal hash character");
        choose(Id::AppendPlaylistFile, append);
        check(paths() == QStringList{a, b, c}, "mixed append adds only the new item in order");
        check(intact(), "mixed append keeps the current item, position and paused state");
        QTest::qWait(80);
        check(!frame.isNull() && frame == video->grabFramebuffer(), "mixed append keeps the rendered paused frame");

        queue.reverseItems();
        search->setText(QStringLiteral("第二段"));
        check(view->model()->rowCount() == 1, "only one item is visible before export");
        const auto out = roundTripPath();
        choose(Id::ExportPlaylistFile, out.chopped(5));
        const auto saved = bytes(out);
        check(saved.startsWith("#EXTM3U\n") && saved.contains("../media/") && !saved.contains(output.absolutePath().toUtf8())
            && PlaylistFile::read(out).paths == roundTripOrder() && intact(), "export adds the extension and saves the complete source order as UTF-8 relative paths");

        const auto atomic = output.filePath("lists/atomic.m3u8");
        const QByteArray original = "previous playlist bytes\r\n";
        write(atomic, original);
        const auto lock = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(atomic).utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "lock previous playlist against replacement");
        const bool failed = !queue.exportPlaylist(atomic);
        CloseHandle(lock);
        check(failed && bytes(atomic) == original, "a failed atomic commit preserves every byte of the previous file");
        check(queue.exportPlaylist(atomic) && bytes(atomic) == saved, "atomic overwrite succeeds once the destination is unlocked");
        check(PlaylistFile::write(output.filePath("media/leading.m3u8"), {c}).isEmpty()
            && bytes(output.filePath("media/leading.m3u8")).contains("./#")
            && PlaylistFile::read(output.filePath("media/leading.m3u8")).paths == QStringList{c}, "export round-trips a relative name beginning with a comment character");

        QTemporaryDir otherVolume;
        const auto other = otherVolume.filePath(QStringLiteral("跨盘 样片.mp4"));
        const auto volume = [](const QString &path) { return path.left(2).toUpper(); };
        if (otherVolume.isValid() && volume(other) != volume(out)) {
            require(QFile::copy(media.filePath("E1.mp4"), other), "prepare cross-volume media");
            queue.addFiles({other}, false);
            const auto cross = output.filePath("lists/cross-drive.m3u8");
            check(queue.exportPlaylist(cross) && bytes(cross).contains(other.toUtf8()) && PlaylistFile::read(cross).paths == paths(),
                "export keeps a file on another Windows volume absolute");
            queue.remove({model.idAt(model.rowCount() - 1)});
        }
        search->clear();
        queue.clear();
        opens.clear();
        choose(Id::AppendPlaylistFile, out);
        check(paths() == roundTripOrder() && opens.size() == 1, "append to an empty queue starts the first item once in exported order");
        pause(model.idAt(0));
        check(!session.preferences().recordHistory, "history disabled before closing the write process");
        main.close();
    }

    void restart()
    {
        check(!session.preferences().recordHistory && !model.rowCount() && player.state() == Player::State::Idle,
            "fresh process with history disabled starts with an empty queue");
        choose(Id::OpenPlaylistFile, roundTripPath());
        check(paths() == roundTripOrder() && opens.size() == 1, "the explicitly exported playlist restores all entries in order after restart");
        pause(model.idAt(0));
    }
};
}

int main(int argc, char **argv)
{
    std::setlocale(LC_NUMERIC, "C");
    if (argc == 2 && QByteArrayView(argv[1]) == MetadataProbe::workerArgument) {
        QFile marker(QFile::decodeName(qgetenv(workerLog)));
        if (!marker.fileName().isEmpty() && marker.open(QIODevice::Append)) { marker.write("1\n"); marker.close(); }
        return MetadataProbe::runWorker();
    }
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName("zopana playlist probe");
    const auto args = app.arguments();
    if (args.size() != 4 || (args[1] != "write" && args[1] != "read")) return 2;
    try {
        Probe probe(args[2], args[3]);
        if (args[1] == "write") {
            probe.searchLargeQueue();
            probe.playbackWithFilter();
            probe.playlistFiles();
        } else {
            probe.restart();
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
