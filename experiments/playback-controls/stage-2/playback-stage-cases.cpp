#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
bool until(auto ready)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 6000) QTest::qWait(10);
    return ready();
}

void check(QJsonObject &report, bool passed, const QString &name)
{
    auto rows = report["checks"].toArray();
    rows.append(QJsonObject{{"name", name}, {"passed", passed}});
    report["checks"] = rows;
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData());
    std::fflush(stdout);
    if (!passed) throw std::runtime_error(name.toStdString());
}

QStringList paths(const QDir &dir, std::initializer_list<const char *> names)
{
    QStringList result;
    for (auto name : names) result.append(dir.filePath(QString::fromUtf8(name)));
    return result;
}

QJsonArray idList(const QList<quint64> &ids)
{
    QJsonArray result;
    for (const auto id : ids) result.append(qint64(id));
    return result;
}

void shuffleCases(const QDir &fixtures, QJsonObject &report)
{
    const auto media = paths(fixtures, {"E1.mp4", "E2.mp4", "E10.mp4"});
    Player player;
    constexpr quint32 seed = 20261114;
    PlaylistController queue(player, nullptr, seed);
    queue.setPlaybackOrder(RepeatMode::All, true);
    const auto ids = queue.addFiles(media, true);
    const QSet<quint64> all(ids.cbegin(), ids.cend());
    check(report, ids.size() == 3, "seeded shuffle queue populated");
    QJsonArray rounds;
    quint64 last = 0;
    for (int index = 0; index < 12; ++index) {
        QList<quint64> round;
        for (int item = 0; item < ids.size(); ++item) {
            if (index || item) queue.next();
            round.append(queue.model().currentId());
        }
        check(report, QSet<quint64>(round.cbegin(), round.cend()) == all && round.first() != last,
              QString("shuffle round %1 visits all IDs without an adjacent repeat").arg(index + 1));
        last = round.last();
        rounds.append(idList(round));
    }
    report["seed"] = qint64(seed);
    report["rounds"] = rounds;
    PlaylistController sameSeed(player, nullptr, seed);
    sameSeed.setPlaybackOrder(RepeatMode::All, true);
    sameSeed.addFiles(media, true);
    const auto secondRunMatches = [&] {
        for (const auto &round : rounds) {
            for (const auto &id : round.toArray()) {
                if (qint64(sameSeed.model().currentId()) != id.toInteger()) return false;
                sameSeed.next();
            }
        }
        return true;
    };
    check(report, secondRunMatches(), "same seed reproduces all 12 rounds");

    const auto selected = queue.model().currentId();
    queue.previous();
    const auto previous = queue.model().currentId();
    queue.cycleRepeatMode();
    queue.next();
    check(report, queue.model().currentId() == selected && previous != selected && queue.shuffleEnabled(),
          "changing repeat after a rollback preserves the forward record");

    queue.setPlaybackOrder(RepeatMode::None, true);
    auto edited = queue.addFiles(media, true);
    queue.next();
    const auto second = queue.model().currentId();
    const auto pending = second == edited[1] ? edited[2] : edited[1];
    queue.previous();
    queue.play(edited.first());
    queue.next();
    check(report, queue.model().currentId() == pending, "explicit selection of the current ID truncates the forward branch");
    check(report, !queue.canNext(), "exhausted no-repeat round offers no next item");

    edited = queue.addFiles(media, true);
    queue.next();
    const auto current = queue.model().currentId();
    const auto removed = current == edited[1] ? edited[2] : edited[1];
    queue.remove({edited.first(), removed});
    check(report, !queue.canPrevious() && !queue.canNext() && queue.model().currentId() == current,
          "removing played and pending IDs prunes both navigation directions");
    const auto added = queue.addFiles({media.first(), queue.model().item(current)->path}, false, 0);
    check(report, added.size() == 1 && !edited.contains(added.first()) && queue.canNext(),
          "re-adding a path joins the current round under a fresh ID");
    check(report, queue.model().moveRows({}, 0, 1, {}, 2) && queue.model().currentId() == current,
          "row reorder preserves the currently playing ID");
    queue.next();
    check(report, queue.model().currentId() == added.first() && !queue.canNext(), "reordered round still draws the one pending ID");
    queue.remove({added.first()});
    check(report, queue.model().currentId() == 0 && !queue.canNext(), "removing the current item leaves the consumed round exhausted");
    queue.setPlaybackOrder(RepeatMode::None, false);
    queue.setPlaybackOrder(RepeatMode::None, true);
    check(report, queue.canNext() && !queue.canPrevious(), "shuffle transition resets the round and history");

    queue.setPlaybackOrder(RepeatMode::All, true);
    queue.addFiles(media, true);
    for (int index = 0; index < PlaylistController::shuffleHistoryLimit + 42; ++index) queue.next();
    int backCount = 0;
    while (queue.canPrevious() && backCount <= PlaylistController::shuffleHistoryLimit) {
        queue.previous();
        ++backCount;
    }
    check(report, backCount == PlaylistController::shuffleHistoryLimit - 1 && !queue.canPrevious(), "long-running history retains only the last 1024 selections");
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("Could not initialize libmpv");
    return player;
}

class PlaybackFixture
{
public:
    explicit PlaybackFixture(QJsonObject &report) : report_(report), window(initialize(player)),
        queue(*window.findChild<PlaylistController *>()), actions(*window.findChild<PlayerActions *>())
    {
        player.setMuted(true);
        window.resize(1120, 720);
        window.show();
        require(until([&] { return window.findChild<VideoWidget *>()->rendererReady(); }), "OpenGL renderer ready");
    }

    void require(bool passed, const QString &name) { check(report_, passed, name); }

    void paused(quint64 id)
    {
        require(until([&] { return player.state() == Player::State::Loaded && player.itemId() == id && player.canSeek(); }), "item ready to seek");
        player.setPaused(true);
        require(until([&] { return player.paused(); }), "engine confirms pause");
    }

    void seek(double seconds)
    {
        player.seek(seconds);
        require(until([&] { return std::abs(player.position() - seconds) < .12; }), "engine reaches seek target");
    }

    void playToEof()
    {
        seek(player.duration() - .20);
        player.setPaused(false);
    }

    QJsonObject &report_;
    Player player;
    MainWindow window;
    PlaylistController &queue;
    PlayerActions &actions;
};

void errorCases(PlaybackFixture &probe, const QDir &fixtures)
{
    auto &queue = probe.queue;
    auto &player = probe.player;
    QList<quint64> failures;
    int completions = 0;
    const auto eof = QObject::connect(&player, &Player::playbackCompleted, &probe.window, [&] { ++completions; });
    const auto failure = QObject::connect(&player, &Player::sessionChanged, &probe.window,
        [&](quint64, quint64 id, Player::State state, const QString &) {
            if (state == Player::State::Error) failures.append(id);
        });
    QSignalSpy summaries(&queue, &PlaylistController::message);

    queue.setPlaybackOrder(RepeatMode::All, true);
    auto ids = queue.addFiles(paths(fixtures, {"E1.mp4", "bad1.mkv", "bad2.mkv"}), true);
    probe.require(ids.size() == 3, "mixed shuffle queue populated");
    probe.paused(ids.first());
    auto identity = player.playbackIdentity();
    probe.playToEof();
    probe.require(until([&] { return failures.size() == 2 && player.state() == Player::State::Loaded && player.itemId() == ids.first(); }),
                  "failed shuffle candidates are skipped across the round boundary");
    probe.require(player.generation() == identity.generation + 3 && failures[0] != failures[1] && summaries.size() == 1 && completions == 1,
                  "mixed shuffle attempts each failure once and summarizes once");
    probe.paused(ids.first());
    probe.require(!queue.canPrevious(), "skipped shuffle candidates are absent from the playback record");

    failures.clear();
    summaries.clear();
    ids = queue.addFiles(paths(fixtures, {"E1.mp4", "bad1.mkv", "bad2.mkv", "bad3.mkv"}), true);
    probe.require(ids.size() == 4, "all-bad continuation fixture populated");
    probe.paused(ids.first());
    const auto removeGood = QObject::connect(&player, &Player::sessionChanged, &probe.window,
        [&](quint64, quint64 id, Player::State state, const QString &) {
            if (state == Player::State::Loading && id != ids.first()) queue.remove({ids.first()});
        });
    identity = player.playbackIdentity();
    probe.playToEof();
    probe.require(until([&] { return failures.size() == 3 && summaries.size() == 1; }), "all-bad shuffle stops after one bounded traversal");
    QTest::qWait(150);
    probe.require(player.generation() == identity.generation + 3 && player.state() == Player::State::Error
                      && QSet<quint64>(failures.cbegin(), failures.cend()).size() == 3 && summaries.size() == 1 && completions == 2,
                  "all-bad traversal neither repeats requests nor emits extra summaries");
    QObject::disconnect(removeGood);

    failures.clear();
    summaries.clear();
    const auto repair = fixtures.filePath("retry.mkv");
    if (QFileInfo::exists(repair)) throw std::runtime_error("Retry fixture already exists");
    probe.require(QFile::copy(fixtures.filePath("E1.mp4"), repair), "isolated retry fixture created");
    queue.setPlaybackOrder(RepeatMode::One, true);
    ids = queue.addFiles({repair, fixtures.filePath("E2.mp4")}, true);
    probe.require(ids.size() == 2, "single-repeat error fixture populated");
    probe.paused(ids.first());
    const auto damage = QObject::connect(&player, &Player::playbackCompleted, &probe.window,
        [&](const Player::PlaybackIdentity &identity, Player::CompletionReason) {
            if (identity.itemId == ids.first()) {
                QFile file(repair);
                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write("Broken stage-2 test media");
            }
        });
    probe.playToEof();
    probe.require(until([&] { return failures == QList<quint64>{ids.first()} && summaries.size() == 1; }),
                  "single-repeat automatic reopen failure stops on the same item");
    QObject::disconnect(damage);
    QTest::qWait(150);
    probe.require(player.itemId() == ids.first() && player.state() == Player::State::Error && completions == 3,
                  "single-repeat error neither completes nor switches to a valid neighbor");
    probe.require(QFile::remove(repair) && QFile::copy(fixtures.filePath("E1.mp4"), repair), "test fixture repaired");
    probe.actions.get(PlayerActions::Retry)->trigger();
    probe.paused(ids.first());
    QObject::disconnect(failure);
    QObject::disconnect(eof);

    queue.setPlaybackOrder(RepeatMode::None, true);
    ids = queue.addFiles(paths(fixtures, {"E1.mp4", "bad1.mkv"}), true);
    probe.paused(ids.first());
    probe.playToEof();
    probe.require(until([&] { return player.state() == Player::State::Error && player.itemId() == ids.last(); }) && queue.canPrevious(),
                  "a terminal shuffle failure can return to the last opened item");
    probe.actions.get(PlayerActions::Previous)->trigger();
    probe.paused(ids.first());
    probe.require(!queue.canNext(), "skipped item does not return through the forward record");
}

void completionCases(const QDir &fixtures, const QDir &output, QJsonObject &report)
{
    SettingsStore settings(output.filePath("config"));
    auto preferences = settings.load();
    preferences.recordHistory = true;
    preferences.resumePolicy = 1;
    check(report, settings.save(preferences), "isolated history-enabled profile saved");
    HistoryStore history(output.filePath("data"));
    PlaybackFixture probe(report);
    auto &player = probe.player;
    auto &queue = probe.queue;
    SessionController session(player, probe.window, settings, history);
    QList<Player::PlaybackIdentity> completions;
    bool reasonsValid = true;
    QObject::connect(&player, &Player::playbackCompleted, &session,
        [&](const Player::PlaybackIdentity &identity, Player::CompletionReason reason) {
            completions.append(identity);
            reasonsValid &= reason == Player::CompletionReason::NaturalEof;
        });

    const auto longFile = fixtures.filePath("long-grid.mp4");
    auto ids = queue.addFiles({longFile, fixtures.filePath("E1.mp4"), fixtures.filePath("E2.mp4")}, true);
    probe.require(ids.size() == 3, "completion queue populated");
    probe.paused(ids.first());
    probe.seek(45);
    session.flushProgress();
    probe.require(history.find(longFile).value_or(HistoryEntry{}).resumable(), "old entry has an actual resume point");
    const auto before = player.playbackIdentity();
    player.stop();
    probe.require(player.state() == Player::State::Idle && completions.isEmpty() && history.find(longFile)->resumable(),
                  "explicit stop leaves the checkpoint and sends no completion");

    queue.play(ids.first());
    probe.paused(ids.first());
    const auto original = player.playbackIdentity();
    player.setRendererReady(false);
    player.setRendererReady(true);
    probe.paused(ids.first());
    const auto restored = player.playbackIdentity();
    probe.require(restored.generation == original.generation && restored.renderEpoch != original.renderEpoch
                  && restored.entry != original.entry && completions.isEmpty(), "renderer restore replaces engine identity without completing playback");
    player.playbackCompleted(original, Player::CompletionReason::NaturalEof);
    player.playbackCompleted(before, Player::CompletionReason::NaturalEof);
    auto wrongEntry = restored;
    wrongEntry.entry = original.entry;
    player.playbackCompleted(wrongEntry, Player::CompletionReason::NaturalEof);
    QTest::qWait(80);
    probe.require(player.playbackIdentity() == restored && player.state() == Player::State::Loaded,
                  "obsolete generation, render epoch and entry do not advance the queue");
    completions.clear(); // The calls above intentionally injected stale signal payloads.

    bool completedBeforeNext = false;
    QObject::connect(&player, &Player::sessionChanged, &session,
        [&](quint64, quint64 id, Player::State state, const QString &) {
            if (id != ids[1] || state != Player::State::Loading) return;
            const HistoryStore reader(output.filePath("data"));
            const auto oldEntry = reader.find(longFile);
            completedBeforeNext = oldEntry && oldEntry->position == 0;
        });
    probe.playToEof();
    probe.paused(ids[1]);
    probe.require(completions == QList<Player::PlaybackIdentity>{restored} && reasonsValid && completedBeforeNext
                      && player.generation() == restored.generation + 1, "matching natural EOF writes history before issuing one next load");
    history.waitForSaved();
    const HistoryStore reader(output.filePath("data"));
    probe.require(reader.find(longFile).has_value() && reader.find(longFile)->position == 0, "completed progress reaches the history file");

    const auto cancelAdvance = QObject::connect(&player, &Player::playbackCompleted, &session,
        [&](const Player::PlaybackIdentity &identity, Player::CompletionReason) {
            if (identity.itemId == ids[1]) player.stop();
        });
    probe.playToEof();
    probe.require(until([&] { return player.state() == Player::State::Idle; }), "stop intercepts queued automatic advance");
    QTest::qWait(100);
    probe.require(player.itemId() == ids[1] && completions.size() == 2, "queued completion never advances after a user stop");
    QObject::disconnect(cancelAdvance);

    const auto count = completions.size();
    queue.play(ids[2]);
    probe.paused(ids[2]);
    probe.require(completions.size() == count, "replacement does not publish a completion for the outgoing item");

    probe.seek(1);
    player.setLoopA();
    probe.seek(2);
    player.setLoopB();
    const auto looping = player.playbackIdentity();
    player.setPaused(false);
    probe.require(until([&] { return player.loopActive() && player.position() < 1.8; }), "A–B wraps within the current entry");
    probe.require(player.playbackIdentity() == looping && completions.size() == count, "A–B wrap does not complete the item");

    const auto bad = queue.addFiles(paths(fixtures, {"bad1.mkv", "E1.mp4"}), true);
    probe.require(until([&] { return player.state() == Player::State::Error; }), "manually opened bad file reports an error");
    QTest::qWait(100);
    probe.require(player.itemId() == bad.first() && completions.size() == count, "manual load error remains retryable without completing or advancing");

    const auto blockedPath = output.filePath("blocked-history");
    QFile blocker(blockedPath);
    probe.require(blocker.open(QIODevice::WriteOnly | QIODevice::NewOnly), "isolated blocking history path created");
    blocker.close();
    SettingsStore blockedSettings(output.filePath("blocked-config"));
    probe.require(blockedSettings.save(preferences), "write-failure profile saved");
    HistoryStore blockedHistory(blockedPath);
    {
        SessionController blockedSession(player, probe.window, blockedSettings, blockedHistory);
        QSignalSpy writeErrors(&blockedHistory, &HistoryStore::error);
        const auto queued = queue.addFiles(paths(fixtures, {"E1.mp4", "E2.mp4"}), true);
        probe.paused(queued.first());
        probe.playToEof();
        probe.paused(queued.last());
        probe.require(writeErrors.size() == 1 && player.itemId() == queued.last(), "history write failure is reported and the queue keeps playing");
    }
    errorCases(probe, fixtures);
}

void preferencesCase(const QString &mode, const QDir &fixtures, const QDir &output, QJsonObject &report)
{
    const auto root = QDir(output.filePath("../profile"));
    SettingsStore settings(root.filePath("config"));
    HistoryStore history(root.filePath("data"));
    PlaybackFixture probe(report);
    SessionController session(probe.player, probe.window, settings, history);
    auto &queue = probe.queue;
    auto &actions = probe.actions;
    const auto state = [&](RepeatMode repeat, bool shuffle) {
        return queue.repeatMode() == repeat && queue.shuffleEnabled() == shuffle
            && actions.repeatModeAction(repeat)->isChecked() && actions.get(PlayerActions::ShuffleToggle)->isChecked() == shuffle;
    };
    probe.require(!session.preferences().recordHistory && queue.model().rowCount() == 0, "history off and queue empty on startup");
    if (mode == "--preferences-write") {
        probe.require(state(RepeatMode::None, false), "new profile starts with no repeat or shuffle");
        actions.get(PlayerActions::OrderRandom)->trigger();
        probe.require(state(RepeatMode::All, true) && until([&] {
            const auto saved = settings.load();
            return saved.repeatMode == RepeatMode::All && saved.shuffleEnabled;
        }), "legacy shortcut persists the two options through the merge timer");
        probe.require(!QFileInfo::exists(history.fileName()), "preference changes create no history file");
        actions.get(PlayerActions::RepeatCycle)->trigger();
        actions.get(PlayerActions::RepeatCycle)->trigger();
        probe.require(state(RepeatMode::One, true), "repeat changes without clearing shuffle");
        probe.window.close();
        const auto saved = settings.load();
        probe.require(saved.repeatMode == RepeatMode::One && saved.shuffleEnabled, "closing flushes the latest repeat and shuffle values");
        return;
    }

    probe.require(state(RepeatMode::One, true) && probe.player.path().isEmpty(), "restart restores both settings before a file is opened");
    const auto path = fixtures.filePath("E1.mp4");
    history.remember(path, 4, 16);
    history.waitForSaved();
    session.clearHistory();
    const auto edits = session.preferences();
    probe.require(history.entries().isEmpty() && state(RepeatMode::One, true) && session.applyPreferences(edits),
                  "clearing history and applying settings preserve playback order");
    history.remember(path, 4, 16);
    probe.require(history.entries().size() == 1, "history sample saved separately");

    QTimer::singleShot(0, &probe.window, [&] {
        auto *dialog = probe.window.findChild<QDialog *>("settingsDialog");
        if (!dialog) { if (auto *modal = QApplication::activeModalWidget()) modal->close(); return; }
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        auto *reset = dialog->findChild<QPushButton *>("resetAllSettingsButton");
        if (!buttons || !reset) { dialog->reject(); return; }
        QTimer::singleShot(0, dialog, [] {
            if (auto *confirm = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                for (auto *button : confirm->buttons()) {
                    if (confirm->buttonRole(button) == QMessageBox::AcceptRole) { button->click(); return; }
                }
                confirm->reject();
            }
        });
        reset->click();
        buttons->button(QDialogButtonBox::Ok)->click();
        if (dialog->isVisible()) dialog->reject();
    });
    actions.get(PlayerActions::Settings)->trigger();
    const auto saved = settings.load();
    probe.require(state(RepeatMode::None, false) && saved.repeatMode == RepeatMode::None && !saved.shuffleEnabled
                      && history.entries().size() == 1, "restore all resets playback order and retains existing history");
    const auto oldConfig = output.filePath("old-config");
    probe.require(QDir().mkpath(oldConfig), "old settings directory created");
    QSettings oldIni(QDir(oldConfig).filePath("settings.ini"), QSettings::IniFormat);
    oldIni.setValue("schemaVersion", 1);
    oldIni.setValue("playlist/order", "random");
    oldIni.sync();
    SettingsStore oldSettings(oldConfig);
    const auto old = oldSettings.load();
    probe.require(old.repeatMode == RepeatMode::None && !old.shuffleEnabled && oldSettings.save(old),
                  "legacy profiles without these keys retain the defaults");
    oldIni.sync();
    probe.require(oldIni.value("playlist/order").toString() == "random", "unknown legacy fields stay intact");
    probe.window.close();
}
}

bool runPlaybackStageCase(const QString &mode, const QDir &fixtures, const QDir &output, QJsonObject &report)
{
    if (mode == "--shuffle") shuffleCases(fixtures, report);
    else if (mode == "--completion") completionCases(fixtures, output, report);
    else if (mode == "--preferences-write" || mode == "--preferences-read") preferencesCase(mode, fixtures, output, report);
    else return false;
    return true;
}
