#include "controlbar.h"
#include "historystore.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "settingsstore.h"
#include "skippolicy.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <limits>
#include <utility>

namespace {
using Reason = Player::CompletionReason;
using State = Player::State;

bool waitFor(const std::function<bool()> &ready, int milliseconds = 7000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < milliseconds) QTest::qWait(10);
    return ready();
}

bool near(double a, double b, double tolerance = .2) { return std::abs(a - b) < tolerance; }

class Report
{
public:
    explicit Report(QString output) : output_(std::move(output)) {}

    void require(bool passed, const QString &description)
    {
        checks_.append(QJsonObject{{"name", description}, {"passed", passed}});
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", description.toUtf8().constData());
        std::fflush(stdout);
        if (!passed) throw std::runtime_error(description.toStdString());
    }

    bool save(const QString &mode, bool passed)
    {
        QFile file(output_ + '/' + mode + ".json");
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(QJsonObject{
            {"mode", mode}, {"passed", passed}, {"qt", qVersion()}, {"checks", checks_}, {"details", details}
        }).toJson()) > 0;
    }

    QJsonObject details;
private:
    QString output_;
    QJsonArray checks_;
};

struct Completion {
    Player::PlaybackIdentity playback;
    Reason reason;
    double position;
};

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("could not initialize libmpv");
    return player;
}

class Suite
{
public:
    Suite(const QString &mode, QDir media, QDir output, Report &report)
        : media(std::move(media)), output(std::move(output)), report(report),
          settings(this->output.filePath((mode == "read" ? "ui" : mode) + "/config")),
          history(this->output.filePath((mode == "read" ? "ui" : mode) + "/data")),
          window(initialize(player)), session(player, window, settings, history),
          queue(*window.findChild<PlaylistController *>()), actions(*window.findChild<PlayerActions *>()),
          video(*window.findChild<VideoWidget *>()), osd(*window.findChild<QLabel *>("playerOsd"))
    {
        player.setMuted(true);
        window.resize(1120, 720);
        window.show(); window.raise(); window.activateWindow();
        video.setFocus();
        report.require(waitFor([&] { return video.rendererReady(); }), "OpenGL renderer ready");
        report.require(QTest::qWaitForWindowActive(&window), "probe window active");
        report.details["dpr"] = video.devicePixelRatioF();
        QObject::connect(&player, &Player::playbackCompleted, &window, [this](const auto &id, Reason reason) {
            completions.append({id, reason, player.position()});
        });
    }

    ~Suite() { player.shutdown(); }

    QString file(const char *name) const { return media.filePath(QString::fromLatin1(name)); }
    QAction *action(PlayerActions::Id id) { return actions.get(id); }
    void check(bool passed, const QString &name) { report.require(passed, name); }
    void ready(const std::function<bool()> &test, const QString &name, int timeout = 7000) { check(waitFor(test, timeout), name); }

    void preferences(bool master, int intro, int outro, bool record = false, int resume = 2)
    {
        auto prefs = session.preferences();
        prefs.skipEnabled = master;
        prefs.introEnabled = intro >= 0;
        prefs.outroEnabled = outro >= 0;
        prefs.introSeconds = std::abs(intro);
        prefs.outroSeconds = std::abs(outro);
        prefs.recordHistory = record;
        prefs.resumePolicy = resume;
        check(session.applyPreferences(prefs), "apply playback preferences");
    }

    QList<quint64> open(std::initializer_list<const char *> names)
    {
        QStringList paths;
        for (const auto *name : names) paths.append(file(name));
        const auto ids = queue.addFiles(paths, true);
        check(ids.size() == qsizetype(names.size()), "playlist accepted all fixture paths");
        return ids;
    }

    void playingAt(quint64 id, double start)
    {
        ready([&] { return player.itemId() == id && player.canSeek() && !player.paused()
            && player.position() >= start - .16 && player.position() < start + 2; },
            QString("entry %1 is playing from %2 s").arg(id).arg(start));
    }

    void pause(bool value)
    {
        player.setPaused(value);
        ready([&] { return player.paused() == value; }, value ? "pause acknowledged" : "play acknowledged");
    }

    void speed(double rate)
    {
        player.setMediaProperty("speed", rate);
        ready([&] { return near(player.mediaProperty("speed").toDouble(), rate, .01); }, "engine confirms playback speed");
    }

    void seek(double seconds)
    {
        player.seek(seconds);
        ready([&] { return !player.seekInProgress() && near(player.position(), seconds); },
              QString("seek settled at %1 s").arg(seconds));
    }

    void complete(Reason reason, Player::PlaybackIdentity playback, qsizetype count)
    {
        ready([&] { return completions.size() >= count; }, "playback completes");
        check(completions.size() == count && completions.last().reason == reason && completions.last().playback == playback,
            QString("one completion with matching identity and reason at %1 s").arg(completions.last().position));
    }

    void checkpoint(const char *name, double seconds)
    {
        check(history.remember(file(name), seconds, 120), "saved a valid fixture checkpoint");
        history.waitForSaved();
    }

    QDir media, output;
    Report &report;
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController session;
    PlaylistController &queue;
    PlayerActions &actions;
    VideoWidget &video;
    QLabel &osd;
    QList<Completion> completions;
};

void ui(Suite &s)
{
    auto *master = s.action(PlayerActions::SkipToggle);
    auto *intro = s.action(PlayerActions::SkipIntro);
    auto *outro = s.action(PlayerActions::SkipOutro);
    auto *settings = s.action(PlayerActions::SkipSettings);
    auto *menu = s.window.findChild<QMenu *>("skipPlaybackMenu");
    s.check(menu && menu->actions().contains(master) && menu->actions().contains(intro)
        && menu->actions().contains(outro) && menu->actions().contains(settings), "Play menu exposes all skip actions");
    s.check(!master->isChecked() && !intro->isChecked() && !outro->isChecked() && !s.session.preferences().recordHistory,
        "new profile starts with all skip switches and history off");
    s.check(settings->shortcuts().contains(QKeySequence(Qt::Key_Apostrophe))
        && master->shortcuts().contains(QKeySequence(Qt::SHIFT | Qt::Key_Apostrophe)) && !master->autoRepeat(),
        "quote shortcuts use viewing scope and suppress repeat");
    const auto bindings = s.actions.resolvedBindings(QVariantMap{{"play.toggle", QStringList{"Shift+'", "'"}}});
    s.check(bindings.value(master->objectName()).toStringList().isEmpty()
        && bindings.value(settings->objectName()).toStringList().isEmpty()
        && bindings.value("play.toggle").toStringList().size() == 2, "existing custom bindings win both quote conflicts");

    bool openedPage = false, ranges = false, textInput = false, draftApplies = false;
    QTimer::singleShot(250, &s.window, [&] {
        auto *dialog = s.window.findChild<QDialog *>("settingsDialog");
        if (!dialog) return;
        openedPage = dialog->findChild<QTabWidget *>("settingsTabs")->currentWidget()->objectName() == "skipSettingsPage";
        auto *h = dialog->findChild<QSpinBox *>("introSecondsSpin");
        auto *e = dialog->findChild<QSpinBox *>("outroSecondsSpin");
        ranges = h->minimum() == 0 && e->maximum() == 86400;
        h->setFocus();
        QTest::keyClick(h, Qt::Key_Apostrophe, Qt::ShiftModifier);
        textInput = !master->isChecked();
        dialog->findChild<QCheckBox *>("skipEnabledCheck")->setChecked(true);
        dialog->findChild<QCheckBox *>("introEnabledCheck")->setChecked(true);
        dialog->findChild<QCheckBox *>("outroEnabledCheck")->setChecked(true);
        h->setValue(10); e->setValue(15);
        draftApplies = !s.session.preferences().skipEnabled;
        dialog->grab().save(s.output.filePath("skip-settings.png"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        if (dialog->isVisible()) dialog->reject();
    });
    s.video.setFocus();
    QTest::keyClick(&s.video, Qt::Key_Apostrophe);
    s.check(openedPage && ranges && textInput && draftApplies, "apostrophe opens the skip tab, validates seconds and keeps edits in the draft");
    s.check(master->isChecked() && intro->isChecked() && outro->isChecked() && intro->text().contains("10") && outro->text().contains("15"),
        "settings apply updates the menu and its seconds");
    s.video.setFocus();
    QTest::keyClick(&s.video, Qt::Key_Apostrophe, Qt::ShiftModifier);
    s.check(!master->isChecked() && intro->isChecked() && outro->isChecked(), "shift apostrophe changes only the master switch");
    outro->trigger();
    s.check(!outro->isChecked() && intro->isChecked(), "submenu toggles the two subfeatures independently");
    outro->trigger();
    s.ready([&] { return !s.settings.load().skipEnabled; }, "menu changes save through the existing merge timer");
    master->trigger();
    s.window.close();
    const auto prefs = s.settings.load();
    s.check(prefs.skipEnabled && prefs.introEnabled && prefs.outroEnabled && prefs.introSeconds == 10 && prefs.outroSeconds == 15,
        "closing flushes the master switch and retains both seconds");
    s.check(!QFile::exists(s.history.fileName()), "saving skip preferences creates no history file");
}

void read(Suite &s)
{
    const auto &p = s.session.preferences();
    s.check(p.skipEnabled && p.introEnabled && p.outroEnabled && p.introSeconds == 10 && p.outroSeconds == 15
        && s.action(PlayerActions::SkipToggle)->isChecked() && s.queue.model().rowCount() == 0,
        "independent empty-queue process restores both seconds and all switches");
    s.session.clearHistory();
    s.check(s.action(PlayerActions::SkipToggle)->isChecked() && s.settings.load().introSeconds == 10, "clearing history preserves skip preferences");
    QTimer::singleShot(120, &s.window, [&] {
        auto *dialog = s.window.findChild<QDialog *>("settingsDialog");
        if (!dialog) return;
        const auto *buttons = dialog->findChild<QDialogButtonBox *>();
        buttons->button(QDialogButtonBox::RestoreDefaults)->click();
        buttons->button(QDialogButtonBox::Ok)->click();
        if (dialog->isVisible()) dialog->reject();
    });
    s.action(PlayerActions::SkipSettings)->trigger();
    const auto defaults = s.settings.load();
    s.check(!defaults.skipEnabled && !defaults.introEnabled && !defaults.outroEnabled && defaults.introSeconds == 0 && defaults.outroSeconds == 0,
        "restore this page resets all five skip preferences");
}

void start(Suite &s)
{
    int longLoads = 0, advances = 0;
    bool audioAvailable = false;
    QObject::connect(&s.player, &Player::requestSubmitted, &s.window, [&](quint64, quint64, quint64, const QString &op) {
        if (op.startsWith("loadfile ")) ++longLoads;
    });
    QObject::connect(&s.player, &Player::playbackAdvanced, &s.window, [&] { ++advances; });
    QObject::connect(&s.player, &Player::audioAvailableChanged, &s.window, [&](bool available) { audioAvailable = available; });
    s.preferences(true, 10, 15);
    auto id = s.open({"long.mp4"}).first();
    s.playingAt(id, 10);
    s.check(near(s.player.duration(), 120, .25) && longLoads == 1, "full timeline remains near 120 seconds after one real load");
    for (const auto &[intro, outro] : {std::pair{10,-15}, std::pair{-10,15}, std::pair{-10,-15}}) {
        s.preferences(true, intro, outro);
        id = s.open({"long.mp4"}).first();
        s.playingAt(id, intro > 0 ? intro : 0);
        s.pause(true);
        s.seek(104);
        const auto playback = s.player.playbackIdentity();
        const auto count = s.completions.size();
        s.speed(3.0);
        s.pause(false);
        if (outro > 0) s.complete(Reason::SkippedOutro, playback, count + 1);
        else {
            s.ready([&] { return s.player.position() > 106; }, "playback passes the configured outro when that switch is off");
            s.check(s.completions.size() == count, "disabled outro produces no logical completion");
        }
    }
    s.preferences(false, 10, 15);
    id = s.open({"long.mp4"}).first();
    s.playingAt(id, 0);
    s.check(s.session.preferences().introSeconds == 10 && s.session.preferences().outroSeconds == 15, "master off retains the configured segment values");
    s.preferences(true, 0, 0);
    id = s.open({"short.mp4"}).first();
    s.playingAt(id, 0);
    s.pause(true);
    s.seek(19);
    auto playback = s.player.playbackIdentity();
    auto count = s.completions.size();
    s.pause(false);
    s.complete(Reason::NaturalEof, playback, count + 1);

    s.preferences(true, 15, 10);
    const auto shortList = s.open({"short.mp4", "E1.mp4", "E2.mp4"});
    s.playingAt(shortList.first(), 0);
    s.check(near(s.player.duration(), 20, .25) && s.osd.isVisible() && s.osd.text().contains(QStringLiteral("超出")), "overlapping ranges fall back and show the one-file hint");
    s.osd.hide();
    QTest::qWait(200);
    s.check(!s.osd.isVisible() && s.queue.model().currentId() == shortList.first(), "short media keeps playing with no repeated warning or queue jump");
    s.preferences(true, 2, 5);
    id = s.open({"audio.m4a"}).first();
    s.playingAt(id, 2);
    s.pause(true);
    s.seek(14);
    playback = s.player.playbackIdentity(); count = s.completions.size();
    s.pause(false);
    s.complete(Reason::SkippedOutro, playback, count + 1);

    s.preferences(true, 60, 40, true, 0);
    const auto openPrompt = [&](double position) {
        s.player.stop();
        s.checkpoint("long.mp4", position);
        const auto selected = s.open({"long.mp4"}).first();
        s.ready([&] { return s.session.resumePending() && s.player.paused()
            && s.window.findChild<QWidget *>("resumePrompt")->isVisible(); }, "resume question appears during engine pause");
        return selected;
    };
    id = openPrompt(45);
    const int beforePrompt = advances;
    QTest::qWait(450);
    s.check(s.player.paused() && s.player.position() < .1 && advances == beforePrompt && audioAvailable, "audio/video fixture remains paused at zero while waiting for the choice");
    s.window.findChild<QPushButton *>("resumeContinueButton")->click();
    s.playingAt(id, 45);
    id = openPrompt(84);
    s.window.findChild<QPushButton *>("resumeContinueButton")->click();
    s.playingAt(id, 84);
    s.check(s.osd.text().contains(QStringLiteral("断点已在片尾")), "resume inside the outro keeps it playable and displays a hint");
    id = openPrompt(45);
    s.window.findChild<QPushButton *>("resumeStartButton")->click();
    s.playingAt(id, 60);
    id = openPrompt(45);
    s.ready([&] { return !s.session.resumePending() && !s.player.paused() && near(s.player.position(), 60, 1.3); },
        "the eight-second prompt timeout uses the same intro start", 9500);
    id = openPrompt(45);
    s.player.seek(12);
    s.playingAt(id, 12);
    s.check(!s.session.resumePending() && !s.window.findChild<QWidget *>("resumePrompt")->isVisible(), "manual seek cancels the prompt and owns the start");
    s.player.stop();
    s.checkpoint("long.mp4", 45);
    s.preferences(true, 60, 40, true, 1);
    id = s.open({"long.mp4"}).first();
    s.playingAt(id, 45);
    s.check(!s.session.resumePending(), "always resume starts at the original point before the intro threshold");
    s.player.stop();

    s.preferences(true, 60, 40, true, 0);
    id = openPrompt(45);
    const auto pending = s.player.playbackIdentity();
    s.player.setRendererReady(false);
    s.player.setRendererReady(true);
    s.ready([&] { return s.player.canSeek() && s.player.renderEpoch() != pending.renderEpoch
        && s.session.resumePending() && s.player.paused(); }, "renderer recovery keeps the original resume choice pending");
    s.window.findChild<QPushButton *>("resumeContinueButton")->click();
    s.playingAt(id, 45);
    s.check(s.player.generation() == pending.generation, "the recovered prompt still honors the checkpoint in the same playback");
}

void queueCases(Suite &s)
{
    s.preferences(true, 10, 15, true);
    s.speed(3.0);
    for (auto repeat : {RepeatMode::None, RepeatMode::One, RepeatMode::All}) {
        for (bool shuffle : {false, true}) {
            s.queue.setPlaybackOrder(repeat, shuffle);
            const auto ids = s.open({"long.mp4", "E1.mp4"});
            s.playingAt(ids.first(), 10);
            s.pause(true);
            s.seek(103.5);
            const auto playback = s.player.playbackIdentity();
            const auto count = s.completions.size();
            const auto expected = repeat == RepeatMode::One ? ids.first() : ids.last();
            bool clearedBeforeLoad = false;
            const auto watch = QObject::connect(&s.player, &Player::sessionChanged, &s.window, [&](quint64 gen, quint64 id, State state, const QString &) {
                if (gen == playback.generation + 1 && id == expected && state == State::Loading) {
                    const HistoryStore disk(s.output.filePath("queue/data"));
                    const auto entry = disk.find(s.file("long.mp4"));
                    clearedBeforeLoad = entry && entry->position == 0;
                }
            });
            s.pause(false);
            s.complete(Reason::SkippedOutro, playback, count + 1);
            s.check(s.completions.last().position >= 104.97 && s.completions.last().position < 106.5,
                "outro completes shortly after the 105-second boundary at 3x");
            s.playingAt(expected, repeat == RepeatMode::One ? 10 : 0);
            s.check(s.player.generation() == playback.generation + 1 && clearedBeforeLoad,
                "repeat/shuffle advances once after saving completion, with the next item's own bounds");
            QObject::disconnect(watch);
            if (repeat == RepeatMode::None) {
                s.pause(true);
                s.seek(15.8);
                const auto final = s.player.playbackIdentity();
                s.pause(false);
                s.complete(Reason::NaturalEof, final, count + 2);
                QTest::qWait(100);
                s.check(s.player.state() == State::Ended && s.player.generation() == final.generation,
                    "no-repeat queue stays on its final item");
            }
        }
    }

    // A replacement issued during completion has already invalidated the queued continuation.
    s.queue.setPlaybackOrder(RepeatMode::All, false);
    auto ids = s.open({"long.mp4", "E1.mp4", "E2.mp4"});
    s.playingAt(ids.first(), 10);
    s.pause(true);
    s.seek(104);
    const auto old = s.player.playbackIdentity();
    auto count = s.completions.size();
    const auto replace = QObject::connect(&s.player, &Player::playbackCompleted, &s.window, [&](const auto &playback, Reason reason) {
        if (playback == old && reason == Reason::SkippedOutro) s.queue.play(ids.last());
    });
    s.pause(false);
    s.complete(Reason::SkippedOutro, old, count + 1);
    s.playingAt(ids.last(), 0);
    QTest::qWait(250);
    s.check(s.player.generation() == old.generation + 1 && s.completions.size() == count + 1,
        "a late stop or completion cannot affect the manually selected next file");
    QObject::disconnect(replace);

    ids = s.open({"long.mp4", "E1.mp4"});
    s.playingAt(ids.first(), 10);
    s.pause(true);
    s.seek(104);
    const auto beforeRebuild = s.player.playbackIdentity();
    count = s.completions.size();
    const auto rebuild = QObject::connect(&s.player, &Player::playbackCompleted, &s.window, [&](const auto &playback, Reason reason) {
        if (playback == beforeRebuild && reason == Reason::SkippedOutro) {
            s.player.setRendererReady(false);
            s.player.setRendererReady(true);
        }
    });
    s.pause(false);
    s.complete(Reason::SkippedOutro, beforeRebuild, count + 1);
    s.playingAt(ids.last(), 0);
    s.check(s.player.generation() == beforeRebuild.generation + 1 && s.player.renderEpoch() > beforeRebuild.renderEpoch,
        "recreating the renderer while history is writing keeps the accepted completion");
    QObject::disconnect(rebuild);

    ids = s.open({"long.mp4", "E1.mp4"});
    s.playingAt(ids.first(), 10);
    s.pause(true);
    s.seek(104);
    const auto stopped = s.player.playbackIdentity();
    count = s.completions.size();
    s.action(PlayerActions::Stop)->trigger();
    QTest::qWait(150);
    s.check(s.player.state() == State::Idle && s.player.generation() == stopped.generation + 1
        && s.completions.size() == count, "explicit stop near the boundary produces no completion or advance");
    const auto bad = s.open({"bad1.mkv", "long.mp4"});
    s.ready([&] { return s.player.state() == State::Error; }, "broken fixture reports a load error");
    s.check(s.player.itemId() == bad.first() && s.completions.size() == count, "load failures are independent of logical completion");
}

void manual(Suite &s)
{
    s.preferences(true, 10, 15);
    s.speed(3.0);
    const quint64 id = s.open({"long.mp4"}).first();
    s.playingAt(id, 10);
    s.pause(true);
    s.action(PlayerActions::SeekStart)->trigger();
    s.ready([&] { return s.player.position() < .1; }, "Home seeks back to the real beginning");
    s.pause(false);
    s.ready([&] { return s.player.position() > 1; }, "intro plays after a manual seek to zero");
    s.check(s.player.position() < 10, "the intro is not jumped twice");
    s.pause(true);

    auto *slider = s.window.findChild<QSlider *>("seekSlider");
    auto *controls = s.window.findChild<ControlBar *>();
    const auto nearEnd = QPoint(slider->width() - slider->width() / 14, slider->height() / 2);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, nearEnd);
    QTest::qWait(120);
    s.check(controls->seekPreviewActive() && s.player.position() < 10 && s.completions.isEmpty(), "drag preview cannot seek or finish before release");
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, nearEnd);
    s.ready([&] { return s.player.paused() && s.player.position() > 106; }, "released drag can enter the outro while paused");
    const auto frame = s.player.position();
    s.player.frameStep();
    s.ready([&] { return s.player.position() > frame && s.player.paused(); }, "single frame step advances without resuming");
    s.pause(false);
    QTest::qWait(250);
    s.check(s.player.position() > frame && s.completions.isEmpty(), "manually entered outro can keep playing");
    s.pause(true);
    s.seek(103.5);
    auto playback = s.player.playbackIdentity();
    s.pause(false);
    s.complete(Reason::SkippedOutro, playback, 1);

    s.playingAt(s.open({"long.mp4"}).first(), 10);
    s.pause(true);
    s.seek(103);
    s.player.setLoopA();
    s.seek(108);
    s.player.setLoopB();
    s.ready([&] { return s.player.loopActive() && s.player.mediaProperty("ab-loop-b").toDouble() > 107; }, "A-B range crosses the configured boundary");
    s.seek(104);
    s.pause(false);
    s.ready([&] { return s.player.position() > 105.6; }, "A-B playback passes the outro threshold");
    s.pause(true);
    s.player.clearLoop();
    s.pause(false);
    QTest::qWait(220);
    s.check(s.player.position() > 105 && s.completions.size() == 1, "clearing A-B inside the outro leaves the current media playing");
    s.pause(true);
    const auto old = s.player.playbackIdentity();
    const auto position = s.player.position();
    s.player.setRendererReady(false);
    s.player.setRendererReady(true);
    s.ready([&] { return s.player.state() == State::Loaded && s.player.renderEpoch() != old.renderEpoch
        && s.player.paused() && near(s.player.position(), position); }, "renderer recovery keeps paused position and the playback generation");
    s.check(s.player.generation() == old.generation && !s.player.completeOutro(old), "obsolete render epoch cannot complete the recovered playback");
    s.pause(false);
    QTest::qWait(250);
    s.check(s.completions.size() == 1 && s.player.position() > position, "render recovery keeps the manual outro override");
    s.pause(true);
    s.seek(104);
    playback = s.player.playbackIdentity();
    s.pause(false);
    s.complete(Reason::SkippedOutro, playback, 2);

    s.playingAt(s.open({"long.mp4"}).first(), 10);
    s.pause(true);
    s.seek(104);
    QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, nearEnd);
    s.pause(false);
    s.ready([&] { return s.player.position() > 106; }, "normal playback can cross the boundary during an uncommitted drag");
    s.check(controls->seekPreviewActive() && s.completions.size() == 2, "drag preview suppresses the logical finish across the boundary");
    s.check(controls->cancelSeekPreview(), "cancelling the drag leaves the real position alone");
    QTest::qWait(200);
    s.check(s.player.state() == State::Loaded && s.completions.size() == 2, "cancelling a drag inside the outro does not immediately switch files");
}

void timing(Suite &s)
{
    s.preferences(true, 10, 15);
    s.speed(3.0);
    s.playingAt(s.open({"long.mp4"}).first(), 10);
    s.pause(true);
    s.preferences(true, 20, 50);
    s.seek(69);
    s.pause(false);
    s.ready([&] { return s.player.position() > 71; }, "changed seconds do not move the active outro boundary");
    s.check(s.completions.isEmpty(), "mid-play edit waits for a real load");
    s.pause(true);
    s.seek(104);
    s.action(PlayerActions::SkipToggle)->trigger();
    s.pause(false);
    s.ready([&] { return s.player.position() > 106; }, "master off immediately lets playback pass its old boundary");
    s.action(PlayerActions::SkipToggle)->trigger();
    s.check(s.completions.isEmpty() && s.player.state() == State::Loaded, "enabling the master does not jump the current media");
    s.playingAt(s.open({"long.mp4"}).first(), 20);
    s.pause(true);
    s.seek(69);
    const auto playback = s.player.playbackIdentity();
    s.pause(false);
    s.complete(Reason::SkippedOutro, playback, 1);
    s.check(near(s.completions.last().position, 70, .6), "new seconds are used on the next real playback");

    const auto disableDuringStart = QObject::connect(&s.player, &Player::requestSubmitted, &s.window,
        [&](quint64, quint64 generation, quint64, const QString &operation) {
            if (!operation.startsWith("seek ")) return;
            QTimer::singleShot(0, &s.window, [&, generation] {
                if (s.player.generation() == generation && s.action(PlayerActions::SkipToggle)->isChecked())
                    s.action(PlayerActions::SkipToggle)->trigger();
            });
        });
    s.playingAt(s.open({"long.mp4"}).first(), 0);
    s.check(!s.action(PlayerActions::SkipToggle)->isChecked() && s.completions.size() == 1,
        "turning off skip while the intro seek is outstanding returns to zero before playback");
    QObject::disconnect(disableDuringStart);

    SkipPolicy bounds{10, 15};
    const auto waiting = bounds.updateBounds(std::numeric_limits<double>::quiet_NaN(), true, 0);
    const auto unseekable = bounds.updateBounds(120, false, 0);
    s.check(waiting == SkipPolicy::Bounds::Waiting && unseekable == SkipPolicy::Bounds::Waiting && !bounds.inOutro(119),
        "unknown duration and unseekable media have no outro boundary");
    s.check(bounds.updateBounds(120, true, 25) == SkipPolicy::Bounds::Ready && bounds.crossedOutro(104, 107),
        "late bounds inside the body handle a multi-second crossing without moving the playback start");
    SkipPolicy late{10, 15};
    s.check(late.updateBounds(120, true, 106) == SkipPolicy::Bounds::Ready && !late.crossedOutro(104, 110)
        && !late.inOutro(119), "duration first arriving inside the outro disables outro skipping for the play");
    SkipPolicy tooShort{15, 10};
    s.check(tooShort.updateBounds(20, true, 0) == SkipPolicy::Bounds::Invalid
        && tooShort.updateBounds(20, true, 0) == SkipPolicy::Bounds::Ready && !tooShort.enabled(),
        "overlapping ranges invalidate once during the play");
}
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    SettingsStore::setApplicationIdentity();
    const auto args = app.arguments();
    if (args.size() != 4) return 2;
    const auto mode = args[1];
    Report report(args[3]);
    try {
        Suite suite(mode, QDir(args[2]), QDir(args[3]), report);
        if (mode == "ui") ui(suite);
        else if (mode == "read") read(suite);
        else if (mode == "start") start(suite);
        else if (mode == "queue") queueCases(suite);
        else if (mode == "manual") manual(suite);
        else if (mode == "timing") timing(suite);
        else return 2;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        report.save(mode, false);
        return 1;
    }
    return report.save(mode, true) ? 0 : 1;
}
