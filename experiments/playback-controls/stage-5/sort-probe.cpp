#include "historystore.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "playlistpanel.h"
#include "sessioncontroller.h"
#include "settingsstore.h"
#include "videowidget.h"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
constexpr char stall[] = "ZOPANA_STAGE5_STALL_WORKER";
constexpr char marker[] = "ZOPANA_STAGE5_WORKER_MARKER";
constexpr auto asc = SortDirection::Ascending;
constexpr auto desc = SortDirection::Descending;
using Field = SortField;

bool waitFor(const std::function<bool()> &ready, int ms = 8000)
{
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    poll.setTimerType(Qt::PreciseTimer);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    poll.start(5);
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return ready();
}

struct Report {
    QJsonArray checks;
    QJsonObject data;
    void check(bool passed, const QString &what)
    {
        checks.append(QJsonObject{{"name", what}, {"passed", passed}});
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", what.toUtf8().constData());
        std::fflush(stdout);
        if (!passed) throw std::runtime_error(what.toStdString());
    }
};

QList<quint64> rows(const PlaylistModel &model)
{
    QList<quint64> ids;
    for (int i = 0; i < model.rowCount(); ++i) ids.append(model.idAt(i));
    return ids;
}

void comparators(Report &report, const QDir &media)
{
    QLocale::setDefault(QLocale::c());
    PlaylistModel model;
    QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    const auto original = model.insert({media.filePath("dir10/第10集.MKV"), media.filePath("dir2/第2集.mp4"),
        media.filePath("dir10/第2集.MP4"), media.filePath("dir2/第20集"), media.filePath("dir2/第1集.MKV"), media.filePath("dir10/第12集.m4a")});
    const auto assign = [&](int n, qint64 bytes, std::optional<qint64> modified, std::optional<qint64> created,
                            QString title, std::optional<double> duration, QSize size) {
        const auto path = model.item(original[n])->path;
        model.setMetadata(original[n], {path, bytes, modified, created}, PlaylistMetadata{
            n == 3 ? PlaylistMetadata::State::Unknown : PlaylistMetadata::State::Read, title, duration, size});
    };
    assign(0, 200, 100, 30, "第12集", 10., {20, 10});
    assign(1, 10, 3, 20, "第2集.MP4", 2., {10, 20});
    assign(2, 10, 3, {}, {}, 2., {10, 20});
    assign(3, -1, {}, {}, {}, {}, {});
    assign(4, 0, 2, 40, "第1集", 12., {5, 10});
    assign(5, 1000, -5000, 1, "第19集", {}, {});
    const QPersistentModelIndex current(model.index(1));
    model.setCurrent(original[1]);
    model.setSubtitles(original[1], {"keep.srt"});
    model.setStatus(original[1], "paused");
    const struct { Field field; const char *label, *ascending, *descending; } cases[] = {
        {Field::FileName, "natural filename", "E B C A F D", "D F A B C E"},
        {Field::Title, "title and filename fallback", "E B C A F D", "F A B C E D"},
        {Field::Extension, "extension and secondary filename", "F E A B C D", "B C A E F D"},
        {Field::Folder, "folder and secondary filename", "E B D C A F", "F A C D B E"},
        {Field::Path, "complete path", "E B D C A F", "F A C D B E"},
        {Field::FileSize, "byte values including zero", "E B C A F D", "F A B C E D"},
        {Field::ModifiedTime, "modified timestamps", "F E B C A D", "A B C E F D"},
        {Field::CreatedTime, "created timestamps", "F B A E C D", "E A B F C D"},
        {Field::Duration, "duration", "B C A E D F", "E A B C D F"},
        {Field::Resolution, "resolution area then width", "E B C A D F", "A B C E D F"},
        {Field::AddedOrder, "original insertion sequence", "A B C D E F", "F E D C B A"},
    };
    for (const auto &test : cases) {
        for (auto direction : {asc, desc}) {
            model.sortItems({Field::AddedOrder, asc});
            if (test.field == Field::AddedOrder) model.reverseItems();
            model.sortItems({test.field, direction});
            QList<quint64> expected;
            for (char letter : QByteArray(direction == asc ? test.ascending : test.descending))
                if (letter != ' ') expected.append(original[letter - 'A']);
            report.check(rows(model) == expected, QString("%1 %2").arg(test.label, direction == asc ? "ascending" : "descending"));
            report.check(current.data(PlaylistModel::IdRole).toULongLong() == original[1]
                && model.currentId() == original[1] && model.item(original[1])->subtitles == QStringList{"keep.srt"}
                && model.item(original[1])->status == "paused", "layout preserves the persistent index and item data");
        }
    }
    model.sortItems({Field::AddedOrder, asc});
    model.reverseItems();
    model.sortItems({Field::FileName, asc});
    report.check(model.rowOf(original[2]) < model.rowOf(original[1]), "text ties follow the pre-sort order rather than the original insertion order");
    const auto same = model.insert({media.filePath("dir2/Name10.mp4"), media.filePath("dir10/nAME10.mp4")}, 0);
    for (auto direction : {asc, desc}) {
        model.sortItems({Field::FileName, direction});
        report.check(model.rowOf(same[0]) < model.rowOf(same[1]), "filename letters compare without case sensitivity");
    }
    model.sortItems({Field::AddedOrder, asc});
    report.check(rows(model).mid(0, original.size()) == original && rows(model).last() == same.last(), "inserting near the top appends to added order");
}

struct Children {
    int count = 0;
    double cpuMs = 0, peakMiB = 0;
};

Children children()
{
    Children result;
    const auto list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (list == INVALID_HANDLE_VALUE) return result;
    const auto close = qScopeGuard([=] { CloseHandle(list); });
    PROCESSENTRY32W entry{sizeof(entry)};
    for (bool found = Process32FirstW(list, &entry); found; found = Process32NextW(list, &entry)) {
        if (entry.th32ParentProcessID != GetCurrentProcessId()) continue;
        const auto process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!process) continue;
        const auto release = qScopeGuard([=] { CloseHandle(process); });
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) continue;
        ++result.count;
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
        if (K32GetProcessMemoryInfo(process, &memory, sizeof(memory)))
            result.peakMiB = qMax(result.peakMiB, double(memory.PeakWorkingSetSize) / (1024 * 1024));
        FILETIME created, exited, kernel, user;
        if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
            const auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
            result.cpuMs += double(ticks(kernel) + ticks(user)) / 10000;
        }
    }
    return result;
}

qint64 engineInteger(Player &player, const char *name)
{
    int64_t value = 0;
    return mpv_get_property(player.handle(), name, MPV_FORMAT_INT64, &value) >= 0 ? value : -1;
}

double processCpuMs()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        throw std::runtime_error("cannot read player CPU time");
    const auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return double(ticks(kernel) + ticks(user)) / 10000;
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("libmpv initialization failed");
    return player;
}

struct Suite {
    QDir media;
    QDir output;
    Report &report;
    SettingsStore settings{output.filePath("config")};
    HistoryStore history{output.filePath("data")};
    Player player;
    MainWindow window{initialize(player)};
    SessionController session{player, window, settings, history};
    PlaylistController &queue = *window.findChild<PlaylistController *>();
    PlaylistModel &model = queue.model();
    PlaylistHost &host = *window.findChild<PlaylistHost *>();
    PlaylistPanel &panel = *window.findChild<PlaylistPanel *>();
    PlayerActions &actions = *window.findChild<PlayerActions *>();
    VideoWidget &video = *window.findChild<VideoWidget *>();
    int transportCommands = 0;

    Suite(QDir media, QDir output, Report &report) : media(media), output(output), report(report)
    {
        player.setMuted(true);
        window.resize(1130, 700);
        window.show();
        window.raise();
        window.activateWindow();
        ready([&] { return video.rendererReady(); }, "OpenGL renderer ready");
        report.data["dpr"] = window.devicePixelRatioF();
        QObject::connect(&player, &Player::requestSubmitted, &window, [this](auto, auto, auto, const QString &command) {
            if (command.startsWith("loadfile") || command.startsWith("seek") || command.startsWith("stop")) ++transportCommands;
        });
    }

    ~Suite() { player.shutdown(); }
    QString file(const QString &name) const { return media.filePath(name); }
    void check(bool condition, const QString &what) { report.check(condition, what); }
    void ready(const std::function<bool()> &condition, const QString &what, int ms = 8000) { check(waitFor(condition, ms), what); }
    QAction *act(PlayerActions::Id id) { return actions.get(id); }
    bool busy() { return act(PlayerActions::CancelSort)->isEnabled(); }
    void loaded(quint64 id)
    {
        ready([&] { return player.itemId() == id && player.state() == Player::State::Loaded && player.canSeek(); },
              "selected item is loaded and seekable");
    }
    QList<quint64> add(const QStringList &names, bool replace = true, int row = -1)
    {
        QStringList paths;
        for (const auto &name : names) paths.append(file(name));
        const auto ids = queue.addFiles(paths, replace, row);
        check(ids.size() == names.size(), "accepted all requested fixture entries");
        return ids;
    }
    void interfaces();
    void mediaAndChanges();
    void shuffleHistory();
    void writePreferences();
    void readPreferences();
    void throughput();
    void workerRecovery();
};

void Suite::interfaces()
{
    host.setVisible(true, false);
    auto *button = panel.findChild<QToolButton *>("playlistSortButton");
    auto *progress = panel.findChild<QProgressBar *>("playlistSortProgress");
    auto *order = panel.findChild<QLabel *>("playlistSortOrder");
    const auto *rightClick = panel.findChild<QMenu *>("playlistContextMenu");
    const auto *mainPlay = window.findChild<QMenu *>("playMenu");
    const auto sameCommand = [&](const QObject *parent) {
        const auto *sorting = parent->findChild<QMenu *>("playlistSortMenu", Qt::FindDirectChildrenOnly);
        return sorting && sorting->actions().contains(act(PlayerActions::SortAgain))
            && sorting->actions().at(1)->menu()->actions().contains(act(PlayerActions::SortDurationDesc));
    };
    check(button && button->isVisible() && rightClick && mainPlay && sameCommand(rightClick) && sameCommand(mainPlay)
        && sameCommand(button), "toolbar, main menu and context menu share sort commands");
    check(!act(PlayerActions::SortDurationDesc)->isEnabled() && act(PlayerActions::SortDurationDesc)->shortcuts().isEmpty(),
        "empty queue doesn't reorder; sorting has no default shortcut");
    const auto ids = add({"未播放 第02集.mp4", "playing-long.mp4", "audio.m4a"});
    model.setSubtitles(ids[1], {file("external.srt")});
    queue.play(ids[1]);
    loaded(ids[1]);
    ready([&] {
        return std::any_of(player.tracks().begin(), player.tracks().end(), [](const auto &track) { return track.external && track.type == "sub"; });
    }, "external subtitle is attached before sorting");
    player.setPaused(true);
    ready([&] { return player.paused(); }, "paused state confirmed");
    player.seek(5);
    ready([&] { return std::abs(player.position() - 5.) < .18; }, "position is five seconds before sorting");
    const auto identity = player.playbackIdentity();
    const auto subtitle = player.mediaProperty("sid");
    const auto position = player.position();
    const auto before = transportCommands;
    auto *selection = panel.view()->selectionModel();
    selection->setCurrentIndex(panel.view()->model()->index(model.rowOf(ids[2]), 0), QItemSelectionModel::ClearAndSelect);
    selection->select(panel.view()->model()->index(model.rowOf(ids[0]), 0), QItemSelectionModel::Select);
    const QSet<quint64> chosen{ids[0], ids[2]};
    for (auto id : {PlayerActions::SortFileNameAsc, PlayerActions::ReverseQueue, PlayerActions::ShuffleQueue, PlayerActions::SortAddedAsc}) {
        act(id)->trigger();
        const auto selected = panel.selectedIds();
        check(QSet<quint64>(selected.begin(), selected.end()) == chosen && panel.selectedId() == ids[2], "selection and current index follow item IDs");
        check(player.playbackIdentity() == identity && player.paused() && player.position() == position && player.mediaProperty("sid") == subtitle
            && model.item(ids[1])->subtitles == QStringList{file("external.srt")} && transportCommands == before,
            "reorder preserves playback, pause, position and subtitle without a transport command");
    }
    check(rows(model) == ids && order->text().contains("添加顺序"), "added order restores the original insertion order and is shown");
    act(PlayerActions::ReverseQueue)->trigger();
    check(rows(model) == (QList<quint64>{ids[2], ids[1], ids[0]}) && order->text() == "手动顺序"
        && !queue.shuffleEnabled(), "reverse directly inverts the visible rows; shuffle mode stays off");
    queue.next();
    loaded(ids[0]);
    check(player.itemId() == ids[0], "ordered Next follows the new visible list");

    window.resize(730, 520);
    ready([&] { return host.overlayVisible() && button->isVisible(); }, "sorting stays available on the narrow overlay");
    host.setDetached(true);
    host.window()->resize(260, 320);
    ready([&] {
        return button->window() == host.window() && button->isVisible()
            && panel.width() == 260 && order->geometry().right() < panel.width();
    }, "sorting fits in the minimum width detached list");
    auto preferences = session.preferences();
    preferences.shortcuts[act(PlayerActions::SortAgain)->objectName()] = QStringList{"Ctrl+Alt+F8"};
    check(session.applyPreferences(preferences), "configured a sort shortcut");
    host.window()->activateWindow();
    panel.view()->setFocus();
    ready([&] { return QApplication::activeWindow() == host.window(); }, "detached list receives focus");
    QTest::keyClick(panel.view(), Qt::Key_F8, Qt::ControlModifier | Qt::AltModifier);
    check(rows(model) == ids, "custom sort shortcut works in the detached list");
    act(PlayerActions::SortDurationDesc)->trigger();
    check(progress->isVisible() && busy(), "detached list shows progress and enables Cancel");
    auto *cancel = panel.findChild<QToolButton *>("playlistCancelSortButton");
    QTest::mouseClick(cancel, Qt::LeftButton);
    check(!busy() && !progress->isVisible() && rows(model) == ids, "visible Cancel button preserves the visible order");
    host.setDetached(false);
    window.resize(1130, 700);
}

void Suite::mediaAndChanges()
{
    host.setVisible(true, false);
    const auto ids = add({"playing-long.mp4", "broken.mkv", "unknown-duration.webm", "cover.m4a",
                          "未播放 第02集.mp4", "rotated-sar.mp4", "audio.m4a"});
    loaded(ids[0]);
    player.setPaused(true);
    ready([&] { return player.paused(); }, "foreground remains available while probing");
    const auto identity = player.playbackIdentity();
    const auto requests = transportCommands;
    QSignalSpy layouts(&model, &QAbstractItemModel::layoutChanged);
    act(PlayerActions::SortDurationAsc)->trigger();
    check(busy() && rows(model) == ids, "metadata sort keeps the initial rows while reading");
    ready([&] { return !busy(); }, "unplayed files sorted by duration");
    const auto *tag = model.item(ids[4]);
    check(tag->metadata.title == "测试媒体标题" && tag->metadata.duration
        && std::abs(*tag->metadata.duration - 16.) < .1 && tag->metadata.resolution == QSize(960, 540), "Unicode title, duration and encoding size match fixture values");
    check(model.item(ids[5])->metadata.resolution == QSize(720, 540), "resolution uses coded size despite SAR and rotation");
    check(model.item(ids[3])->metadata.resolution.isEmpty() && model.item(ids[3])->metadata.duration
        && model.item(ids[6])->metadata.resolution.isEmpty(), "audio and cover art have an unknown video resolution");
    check(!model.item(ids[1])->metadata.duration && !model.item(ids[2])->metadata.duration
        && model.item(ids[2])->metadata.resolution == QSize(320, 180), "broken file and missing duration remain unknown; other fields remain usable");
    check(rows(model).mid(5) == (QList<quint64>{ids[1], ids[2]}) && model.rowOf(ids[4]) < model.rowOf(ids[0]),
        "unknown durations stay last and an unplayed file participates in sorting");
    check(layouts.size() == 1 && transportCommands == requests && player.playbackIdentity() == identity,
        "one layout commit with no foreground transport command");
    check(model.data(model.index(model.rowOf(ids[4])), Qt::ToolTipRole).toString().contains("测试媒体标题")
        && model.data(model.index(model.rowOf(ids[1])), Qt::ToolTipRole).toString().contains("未知"),
        "tooltip exposes known and unknown metadata");
    for (auto id : {PlayerActions::SortTitleDesc, PlayerActions::SortResolutionDesc, PlayerActions::SortSizeDesc,
                    PlayerActions::SortModifiedAsc, PlayerActions::SortCreatedAsc}) {
        act(id)->trigger();
        ready([&] { return !busy(); }, act(id)->text() + " completes");
    }
    check(transportCommands == requests && player.playbackIdentity() == identity, "successive background sorts leave the foreground engine alone");
    const auto beforeDrag = rows(model);
    act(PlayerActions::SortDurationDesc)->trigger();
    std::unique_ptr<QMimeData> drag(model.mimeData({model.index(0)}));
    check(model.dropMimeData(drag.get(), Qt::MoveAction, model.rowCount(), 0, {}), "internal drop moves a row");
    auto afterDrag = beforeDrag;
    afterDrag.move(0, afterDrag.size() - 1);
    check(!busy() && rows(model) == afterDrag && !queue.appliedSort(), "drop invalidates the old sort and labels the list manual");
    act(PlayerActions::SortDurationDesc)->trigger();
    act(PlayerActions::SortAddedAsc)->trigger();
    check(!busy() && rows(model) == ids, "new immediate sort replaces background sort");
    act(PlayerActions::SortDurationAsc)->trigger();
    const auto inserted = add({"multi.mkv"}, false, 1);
    check(!busy() && model.idAt(1) == inserted.first() && !queue.appliedSort(), "explicit insertion cancels the sort and retains its destination");
    act(PlayerActions::SortTitleDesc)->trigger();
    queue.remove({ids[5]});
    check(!busy() && !model.item(ids[5]), "removing an item invalidates its pending result");
    act(PlayerActions::SortDurationAsc)->trigger();
    const auto next = add({"playing-long.mp4", "replacement.mp4"});
    check(!busy() && rows(model) == next && next[0] != ids[0], "replacing the queue invalidates the old item IDs");
    loaded(next[0]);
    act(PlayerActions::SortDurationAsc)->trigger();
    ready([&] { return !busy(); }, "first version cached");
    check(model.item(next[1])->metadata.title == "测试媒体标题", "first version has the original title");
    const auto path = file("replacement.mp4");
    check(QFile::remove(path) && QFile::copy(file("new-version.mp4"), path), "replaced an isolated file at the same pathname");
    act(PlayerActions::SortAgain)->trigger();
    ready([&] { return !busy(); }, "same-path replacement sorted");
    check(model.item(next[1])->metadata.title == "Replacement title"
        && std::abs(model.item(next[1])->metadata.duration.value_or(0) - 3.) < .35, "metadata cache invalidates for the replacement version");
    check(QFile::remove(path) && QFile::copy(file("未播放 第02集.mp4"), path), "restore the first test version");
    bool swappedDuringVerify = false;
    const auto swap = QObject::connect(&queue, &PlaylistController::sortProgress, &window, [&](int done, int total, bool verifying) {
        if (!total || done != total || !verifying || swappedDuringVerify) return;
        swappedDuringVerify = QFile::remove(path) && QFile::copy(file("new-version.mp4"), path);
    });
    act(PlayerActions::SortAgain)->trigger();
    ready([&] { return !busy(); }, "commit rechecks early replies from the read pass");
    QObject::disconnect(swap);
    check(swappedDuringVerify && !model.item(next[1])->metadata.duration && model.item(next[1])->metadata.title.isEmpty(),
        "stale reply discarded when a file changes between reading and commit");
    act(PlayerActions::SortAgain)->trigger();
    ready([&] { return !busy(); }, "changed version is eligible for the next read");
    check(model.item(next[1])->metadata.title == "Replacement title", "new file version does not inherit cached unknown state");
    act(PlayerActions::SortDurationDesc)->trigger();
    queue.clear();
    QTest::qWait(100);
    check(!busy() && !model.rowCount() && !queue.appliedSort(), "clear prevents a late layout commit");
}

void Suite::shuffleHistory()
{
    const auto ids = add({"playing-long.mp4", "未播放 第02集.mp4", "multi.mkv", "broken.mkv"});
    loaded(ids[0]);
    const auto initial = rows(model);
    queue.setPlaybackOrder(RepeatMode::All, true);
    check(rows(model) == initial, "enabling random playback doesn't reorder the list");
    queue.play(ids[1]);
    loaded(ids[1]);
    queue.play(ids[2]);
    loaded(ids[2]);
    queue.reverseItems();
    queue.shuffleItems();
    queue.sortItems({Field::AddedOrder, asc});
    check(queue.shuffleEnabled() && queue.repeatMode() == RepeatMode::All, "reordering leaves the repeat and random mode unchanged");
    queue.previous();
    loaded(ids[1]);
    queue.next();
    loaded(ids[2]);
    check(player.itemId() == ids[2], "back and forward use the same playback history after sorting and shuffling");
    queue.next();
    ready([&] { return player.itemId() == ids[3] && player.state() == Player::State::Error; }, "the remaining unplayed bad file is tried");
    queue.previous();
    loaded(ids[2]);
    queue.remove({ids[3], ids[1]});
    queue.reverseItems();
    queue.previous();
    loaded(ids[0]);
    check(player.itemId() == ids[0], "deleting played and failed rows prunes random history without losing surviving entries");
}

void Suite::writePreferences()
{
    const auto ids = add({"playing-long.mp4", "未播放 第02集.mp4"});
    loaded(ids[0]);
    act(PlayerActions::SortDurationDesc)->trigger();
    ready([&] { return !busy(); }, "chosen sort completed before saving");
    check(!session.preferences().recordHistory, "history remains disabled");
    ready([&] {
        QSettings ini(settings.fileName(), QSettings::IniFormat);
        return ini.value("playlist/sortField").toString() == "duration" && ini.value("playlist/sortDirection").toString() == "descending";
    }, "sorting choice is saved by the merge timer");
    QFile ini(settings.fileName());
    check(ini.open(QIODevice::ReadOnly) && !ini.readAll().contains("playing-long.mp4") && !QFile::exists(output.filePath("data/history.json")),
        "settings and history do not save queue paths or metadata");
    window.close();
}

void Suite::readPreferences()
{
    check(queue.lastSort() == PlaylistSort{Field::Duration, desc} && !model.rowCount() && !queue.appliedSort()
        && act(PlayerActions::SortDurationDesc)->isChecked(), "fresh process restores condition with an empty, unsorted playlist");
    const auto ids = add({"未播放 第02集.mp4", "playing-long.mp4"});
    check(rows(model) == ids, "restored preference doesn't auto-sort added files");
    act(PlayerActions::SortAgain)->trigger();
    ready([&] { return !busy(); }, "apply restored sort condition explicitly");
    check(model.idAt(0) == ids[1], "restored descending duration sort is applied");
}

void Suite::throughput()
{
    host.setVisible(true, false);
    auto names = QDir(media.filePath("bulk")).entryList({"*.mp4"}, QDir::Files, QDir::Name);
    check(names.size() == 999, "999 immutable paths plus the foreground clip");
    for (auto &name : names) name.prepend("bulk/");
    names.prepend("playing-long.mp4");
    const auto ids = add(names);
    loaded(ids[0]);
    ready([&] { return player.position() > .5 && !player.paused() && engineInteger(player, "aid") > 0; },
          "foreground video and audio are running");
    const auto identity = player.playbackIdentity();
    const auto commands = transportCommands;
    const auto beforeDecode = engineInteger(player, "decoder-frame-drop-count");
    const auto beforeOutput = engineInteger(player, "frame-drop-count");
    int frameSwaps = 0;
    const auto frames = QObject::connect(&video, &VideoWidget::frameSwapped, &window, [&] { ++frameSwaps; });
    const auto endFrames = qScopeGuard([&] { QObject::disconnect(frames); });

    QElapsedTimer clock, turn;
    qint64 worstGap = 0, commitUs = -1, worstInput = 0, inputSentAt = -1;
    int inputs = 0;
    int layoutCount = 0;
    Children sampled;
    QTimer pulse, processSample, inputSample;
    pulse.setTimerType(Qt::PreciseTimer);
    processSample.setInterval(250);
    inputSample.setInterval(250);
    QObject::connect(&pulse, &QTimer::timeout, &window, [&] { worstGap = qMax(worstGap, turn.restart()); });
    QObject::connect(&processSample, &QTimer::timeout, &window, [&] {
        const auto current = children();
        sampled.count = qMax(sampled.count, current.count);
        sampled.peakMiB = qMax(sampled.peakMiB, current.peakMiB);
        sampled.cpuMs = qMax(sampled.cpuMs, current.cpuMs);
    });
    QElapsedTimer commit;
    const auto startCommit = QObject::connect(&model, &QAbstractItemModel::layoutAboutToBeChanged, &window, [&] { commit.start(); });
    const auto endCommit = QObject::connect(&model, &QAbstractItemModel::layoutChanged, &window, [&] {
        ++layoutCount;
        commitUs = commit.nsecsElapsed() / 1000;
    });
    const auto layoutConnections = qScopeGuard([&] { QObject::disconnect(startCommit); QObject::disconnect(endCommit); });

    auto *selection = panel.view()->selectionModel();
    const auto inputResponse = QObject::connect(selection, &QItemSelectionModel::currentChanged, &inputSample, [&] {
        if (inputSentAt < 0) return;
        worstInput = qMax(worstInput, clock.elapsed() - inputSentAt);
        inputSentAt = -1;
        ++inputs;
    });
    const auto disconnectInput = qScopeGuard([&] { QObject::disconnect(inputResponse); });
    bool moveDown = true;
    QObject::connect(&inputSample, &QTimer::timeout, &window, [&] {
        if (inputSentAt >= 0) return;
        const int row = qBound(0, panel.view()->currentIndex().row() + (moveDown ? 1 : -1), model.rowCount() - 1);
        moveDown = !moveDown;
        const auto point = panel.view()->visualRect(panel.view()->model()->index(row, 0)).center();
        inputSentAt = clock.elapsed();
        auto *viewport = panel.view()->viewport();
        QCoreApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonPress, point, viewport->mapToGlobal(point),
                                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier));
        QCoreApplication::postEvent(viewport, new QMouseEvent(QEvent::MouseButtonRelease, point, viewport->mapToGlobal(point),
                                                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
    });
    const auto focused = ids[300], anchored = ids[270];
    selection->setCurrentIndex(panel.view()->model()->index(model.rowOf(focused), 0), QItemSelectionModel::ClearAndSelect);
    panel.view()->scrollTo(panel.view()->model()->index(model.rowOf(anchored), 0), QAbstractItemView::PositionAtTop);
    QTest::qWait(20);
    const auto topId = [&] { return panel.view()->indexAt(QPoint(panel.view()->viewport()->width() / 2, 1)).data(PlaylistModel::IdRole).toULongLong(); };
    const auto anchorBefore = topId();
    clock.start();
    act(PlayerActions::SortFileNameDesc)->trigger();
    const double filenameMs = clock.nsecsElapsed() / 1e6;
    check(anchorBefore == anchored && topId() == anchored && panel.selectedId() == focused,
        "large reorder keeps the top visible item and the current selection by ID");
    panel.view()->scrollTo(selection->currentIndex(), QAbstractItemView::PositionAtCenter);
    report.data["filenameSortMs"] = filenameMs;
    check(filenameMs < 100., "1,000-row natural sorting and reordering complete within 100 ms");

    const auto samplePlayback = [&](const QString &key, bool scanning) {
        constexpr int interval = 6000;
        const auto before = processCpuMs();
        const auto decode = engineInteger(player, "decoder-frame-drop-count");
        const auto output = engineInteger(player, "frame-drop-count");
        const auto swaps = frameSwaps;
        const double position = player.position();
        QElapsedTimer elapsed; elapsed.start();
        double workingSet = 0, privateUsage = 0;
        int count = 0, workerCountPeak = 0;
        do {
            QTest::qWait(250);
            PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
            if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
                throw std::runtime_error("cannot sample player memory");
            workingSet += double(memory.WorkingSetSize) / 1048576;
            privateUsage += double(memory.PrivateUsage) / 1048576;
            workerCountPeak = qMax(workerCountPeak, children().count);
            ++count;
        } while (elapsed.elapsed() < interval && busy() == scanning);
        const auto ms = elapsed.elapsed();
        report.data[key] = QJsonObject{{"sampleMs", ms}, {"cpuPercentOneCore", 100 * (processCpuMs() - before) / ms},
            {"averageWorkingSetMiB", workingSet / count}, {"averagePrivateMiB", privateUsage / count},
            {"workerCountPeak", workerCountPeak}, {"frameSwaps", frameSwaps - swaps}, {"playedSeconds", player.position() - position},
            {"decoderDrops", engineInteger(player, "decoder-frame-drop-count") - decode}, {"outputDrops", engineInteger(player, "frame-drop-count") - output}};
        check(ms >= interval && busy() == scanning && workerCountPeak == (scanning ? 1 : 0) &&
              player.playbackIdentity() == identity && transportCommands == commands && !player.paused(),
              key + " recorded with continuous foreground playback and the expected worker count");
    };

    const auto run = [&](PlayerActions::Id id, const QString &key) {
        const double position = player.position();
        const auto oldSwaps = frameSwaps;
        layoutCount = 0;
        commitUs = -1;
        inputSentAt = -1;
        clock.restart();
        act(id)->trigger();
        if (key == "uncachedScan") samplePlayback("withScan", true);
        ready([&] { return !busy(); }, key + " finishes", 90000);
        report.data[key + "Ms"] = clock.elapsed();
        report.data[key + "LayoutUs"] = commitUs;
        report.data[key + "PlaybackSeconds"] = player.position() - position;
        report.data[key + "FrameSwaps"] = frameSwaps - oldSwaps;
        check(layoutCount == 1 && commitUs >= 0 && commitUs < 100000, key + " commits one responsive layout");
        const auto seconds = clock.elapsed() / 1000.0;
        check((seconds < .6 || (player.position() - position > seconds - .6 && frameSwaps > oldSwaps))
            && player.playbackIdentity() == identity && transportCommands == commands && !player.paused(),
            key + " keeps the foreground playing");
    };
    samplePlayback("withoutScan", false);
    turn.start();
    pulse.start(10);
    processSample.start();
    inputSample.start();
    run(PlayerActions::SortDurationAsc, "uncachedScan");
    run(PlayerActions::SortDurationDesc, "cachedScan");
    pulse.stop();
    processSample.stop();
    inputSample.stop();
    report.data["workerCountPeak"] = sampled.count;
    report.data["workerPeakWorkingSetMiB"] = sampled.peakMiB;
    report.data["workerCpuSampledMs"] = sampled.cpuMs;
    report.data["eventLoopMaxGapMs"] = worstGap;
    report.data["inputResponseMaxMs"] = worstInput;
    report.data["inputResponses"] = inputs;
    report.data["decoderDroppedFrames"] = engineInteger(player, "decoder-frame-drop-count") - beforeDecode;
    report.data["outputDroppedFrames"] = engineInteger(player, "frame-drop-count") - beforeOutput;
    check(sampled.count == 1 && waitFor([] { return children().count == 0; }), "one worker handles the batch and is reaped afterward");
    check(inputs > 0 && worstInput < 100, "posted playlist clicks change selection within the 100 ms interaction target");
    check(model.idAt(0) == ids[0] && model.item(ids.last())->metadata.duration.has_value(), "uncached then cached sort use real durations for unplayed files");
}

void Suite::workerRecovery()
{
    const auto ids = add({"playing-long.mp4", "未播放 第02集.mp4", "multi.mkv"});
    loaded(ids[0]);
    ready([&] { return player.position() > .5; }, "foreground running before blocking the worker");
    const auto identity = player.playbackIdentity();
    const auto busyMarker = output.filePath("worker-entered");
    qputenv(marker, QFile::encodeName(busyMarker));
    const auto resetEnvironment = qScopeGuard([] { qunsetenv(marker); qunsetenv(stall); });
    const auto block = [&] {
        QFile::remove(busyMarker);
        qputenv(stall, "1");
        act(PlayerActions::SortDurationAsc)->trigger();
        ready([&] { return QFile::exists(busyMarker) && children().count == 1; }, "one worker entered blocking read simulation");
        qunsetenv(stall);
    };
    block();
    QElapsedTimer elapsed;
    elapsed.start();
    act(PlayerActions::CancelSort)->trigger();
    check(!busy() && rows(model) == ids, "cancel immediately preserves the visible order");
    ready([] { return children().count == 0; }, "blocked worker is reaped after cancel", 1500);
    report.data["cancelAndReapMs"] = elapsed.elapsed();
    block();
    const double before = player.position();
    elapsed.restart();
    ready([&] { return !busy(); }, "per-item timeout advances to the remaining files", 8000);
    report.data["timeoutThenFinishMs"] = elapsed.elapsed();
    check(!model.item(ids[0])->metadata.duration && model.item(ids[1])->metadata.duration
        && player.playbackIdentity() == identity && player.position() - before > 4.,
        "timeout leaves unknown metadata and allows the foreground and remaining reads to proceed");
    ready([] { return children().count == 0; }, "timed-out worker and successor are reaped", 1500);
    block();
    elapsed.restart();
    window.close();
    ready([] { return children().count == 0; }, "closing the window reaps a worker blocked in a read", 1500);
    report.data["closeAndReapMs"] = elapsed.elapsed();
}
}

int main(int argc, char **argv)
{
    std::setlocale(LC_NUMERIC, "C");
    if (argc == 2 && QByteArrayView(argv[1]) == MetadataProbe::workerArgument) {
        if (qEnvironmentVariableIsSet(stall)) {
            QFile entered(qEnvironmentVariable(marker));
            if (!entered.open(QIODevice::WriteOnly)) return 2;
            entered.close();
            Sleep(INFINITE);
        }
        return MetadataProbe::runWorker();
    }
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4) return 2;
    const auto mode = args[1];
    const QDir media(args[2]), output(args[3]);
    Report report;
    bool passed = false;
    try {
        if (mode == "comparators") comparators(report, media);
        else {
            Suite suite(media, output, report);
            if (mode == "ui") {
                suite.interfaces();
                suite.mediaAndChanges();
                suite.shuffleHistory();
            } else if (mode == "write") suite.writePreferences();
            else if (mode == "read") suite.readPreferences();
            else if (mode == "throughput") suite.throughput();
            else if (mode == "recovery") suite.workerRecovery();
            else return 2;
        }
        passed = true;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
    }
    QFile results(output.absoluteFilePath(mode + ".json"));
    if (!results.open(QIODevice::WriteOnly) || results.write(QJsonDocument(QJsonObject{
        {"mode", mode}, {"passed", passed}, {"checks", report.checks}, {"details", report.data}
    }).toJson()) < 0) return 2;
    return passed ? 0 : 1;
}
