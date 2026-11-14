#include "captureservice.h"
#include "historystore.h"
#include "mainwindow.h"
#include "metadataprobe.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

namespace {
using Actions = PlayerActions;
using Reason = Player::CompletionReason;
using Start = Player::StartPosition;
using State = Player::State;
constexpr PlaylistSort durationAscending{SortField::Duration, SortDirection::Ascending};

struct Report {
    QJsonArray checks;
    QJsonObject details;
    void require(bool passed, const QString &name)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", passed}});
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData());
        std::fflush(stdout);
        if (!passed) throw std::runtime_error(name.toStdString());
    }
    template<class Condition> void wait(Condition ready, const QString &name, int timeout = 7000)
    {
        QElapsedTimer timer; timer.start();
        while (!ready() && timer.elapsed() < timeout) QTest::qWait(10);
        require(ready(), name);
    }
};

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("libmpv did not initialize");
    return player;
}

bool closeTo(double a, double b, double tolerance = .05) { return std::abs(a - b) < tolerance; }

QJsonObject resources()
{
    const auto process = GetCurrentProcess();
    DWORD handles = 0;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessHandleCount(process, &handles) ||
        !GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
        throw std::runtime_error("could not sample process resources");
    return {{"privateMiB", double(memory.PrivateUsage) / 1048576}, {"workingSetMiB", double(memory.WorkingSetSize) / 1048576},
            {"handles", int(handles)}, {"gdi", int(GetGuiResources(process, GR_GDIOBJECTS))},
            {"user", int(GetGuiResources(process, GR_USEROBJECTS))}};
}

class Suite
{
public:
    Suite(QDir fixtures, QDir output, QString profile, Report &report)
        : fixtures(fixtures), output(output), profile(profile), report(report),
          settings(profile + "/config"), history(profile + "/data"), window(initialize(player)), session(player, window, settings, history),
          queue(*window.findChild<PlaylistController *>()), actions(*window.findChild<Actions *>()),
          host(*window.findChild<PlaylistHost *>()), video(*window.findChild<VideoWidget *>())
    {
        window.show();
        report.wait([&] { return video.rendererReady(); }, "OpenGL initialized");
        report.details["dpr"] = video.devicePixelRatioF();
        QObject::connect(&player, &Player::startPositionResolved, &window, [this](auto id, auto reason, bool success) {
            if (success) starts[id.generation].append(reason);
        });
        QObject::connect(&player, &Player::playbackCompleted, &window, [this](auto id, auto reason) {
            completions[id.generation].append(reason);
        });
    }

    ~Suite()
    {
        window.close();
        player.shutdown();
    }

    void dialog(bool merged)
    {
        bool applied = false;
        QTimer::singleShot(0, &window, [&] {
            auto *box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!box) { QApplication::closeAllWindows(); return; }
            box->findChild<QSpinBox *>("seekSmallSpin")->setValue(7);
            box->findChild<QSpinBox *>("seekMediumSpin")->setValue(27);
            box->findChild<QSpinBox *>("seekLargeSpin")->setValue(71);
            box->findChild<QLineEdit *>("captureDirectoryEdit")->setText(output.absoluteFilePath("captures"));
            box->findChild<QLineEdit *>("audioLanguagesEdit")->setText("eng");
            box->findChild<QLineEdit *>("subtitleLanguagesEdit")->setText("eng");
            box->findChild<QComboBox *>("resumePolicyCombo")->setCurrentIndex(1);
            box->findChild<QCheckBox *>("skipEnabledCheck")->setChecked(true);
            box->findChild<QCheckBox *>("introEnabledCheck")->setChecked(true);
            box->findChild<QCheckBox *>("outroEnabledCheck")->setChecked(true);
            box->findChild<QSpinBox *>("introSecondsSpin")->setValue(4);
            box->findChild<QSpinBox *>("outroSecondsSpin")->setValue(6);
            box->findChild<QCheckBox *>("resetViewOnOpenCheck")->setChecked(true);
            if (merged) {
                player.setMediaProperty("speed", 1.75);
                queue.setPlaybackOrder(RepeatMode::All, true);
            }
            box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
            applied = session.preferences().seekSmallSeconds == 7 && session.preferences().introSeconds == 4;
            box->findChild<QSpinBox *>("seekSmallSpin")->setValue(19);
            box->reject();
        });
        session.showSettings();
        report.require(applied && session.preferences().seekSmallSeconds == 7, "dialog applied draft and cancelled further edits");
    }

    void combination()
    {
        report.require(!session.preferences().recordHistory, "fresh profile has opt-in history disabled");
        dialog(false);
        auto preferences = session.preferences();
        preferences.recordHistory = true;
        preferences.resumePolicy = 0;
        report.require(session.applyPreferences(preferences), "history opt-in applied");
        const auto a = file("长片 A.mp4"), b = file("长片 B.mp4");
        report.require(history.remember(a, 48, 137), "saved a valid resume point");
        history.waitForSaved();
        trigger(Actions::OrderSingle);
        trigger(Actions::ShuffleToggle);
        const auto ids = queue.addFiles({a, b}, true);
        report.require(ids.size() == 2, "two titled files added");
        auto *resume = window.findChild<QPushButton *>("resumeContinueButton");
        report.wait([&] { return resume->isVisible() && session.resumePending() && player.paused(); }, "visible resume prompt holds the first video");
        player.setVolume(5);
        const auto original = player.playbackIdentity();
        resume->click();
        report.wait([&] { return starts.value(original.generation) == QList{Start::Resume} && player.position() >= 47.9; },
                    "resume is the only initial seek");
        player.setPaused(true);
        report.wait([&] { return player.paused() && video.canCaptureTransform(player.videoTransformVersion()); }, "pause and current video transform");
        const auto position = player.position();
        trigger(Actions::SortDurationAsc);
        trigger(Actions::RotateNext);
        trigger(Actions::Scale100);
        report.wait([&] { return queue.appliedSort() == durationAscending && player.videoRotation() == 90
                          && video.canCaptureTransform(player.videoTransformVersion()); }, "background sort, rotation and 100% scaling settled");
        auto *osd = window.findChild<QLabel *>("playerOsd");
        report.require(queue.model().rowOf(ids[1]) == 0 && player.playbackIdentity() == original && player.paused()
                       && closeTo(player.position(), position) && window.lastVideoScale() == 1
                       && actions.get(Actions::OrderSingle)->isChecked() && actions.get(Actions::ShuffleToggle)->isChecked()
                       && actions.get(Actions::SortDurationAsc)->isChecked() && actions.get(Actions::SkipToggle)->isChecked()
                       && osd && !osd->text().isEmpty(), "sort preserved playback, with matching actions and feedback");

        crossOutro();
        waitMedia(ids[0], original.generation + 1);
        report.wait([&] { return starts.value(player.generation()) == QList{Start::SkipIntro}; },
                    "single-item repeat starts once at the intro even with shuffle enabled");
        const auto recent = history.find(a);
        report.require(completions.value(original.generation) == QList{Reason::SkippedOutro} && recent && !recent->resumable(),
                       "outro consumed once and cleared the saved resume point");

        preferences = session.preferences();
        preferences.recordHistory = false;
        report.require(session.applyPreferences(preferences), "history switched off while playing");
        trigger(Actions::OrderList);
        trigger(Actions::ShuffleToggle);
        const auto repeated = player.generation();
        crossOutro();
        waitMedia(ids[1], repeated + 1);
        report.wait([&] { return starts.value(player.generation()) == QList{Start::SkipIntro}; }, "sorted queue wraps once with history off");
        QTest::qWait(250);
        report.require(player.generation() == repeated + 1 && completions.value(repeated) == QList{Reason::SkippedOutro}
                       && !session.resumePending() && !history.find(b).has_value(), "no double advance, stale prompt or history write");
    }

    void write()
    {
        report.require(!session.preferences().recordHistory && !QFile::exists(profile + "/config/settings.ini"), "new isolated profile");
        player.setMediaProperty("speed", 1.25);
        dialog(true);
        report.require(closeTo(player.propertyTarget("speed").toDouble(), 1.75) && queue.repeatMode() == RepeatMode::All
                       && queue.shuffleEnabled(), "applying the draft kept the latest runtime order and speed");
        auto preferences = session.preferences();
        const auto binding = actions.get(Actions::MediaInfo)->objectName();
        preferences.shortcuts[binding] = QStringList{"Ctrl+Alt+F8"};
        report.require(session.applyPreferences(preferences), "custom binding applied");
        player.setMediaProperty("audio-pitch-correction", false);
        player.setMediaProperty("sub-visibility", false);
        player.setMediaProperty("audio-delay", .145);
        player.setMediaProperty("sub-delay", -.225);
        player.setMediaProperty("sub-scale", 1.4);
        player.setAudioPreferences(37, true);
        trigger(Actions::StandardMode);
        trigger(Actions::AlwaysOnTop);
        const auto ids = queue.addFiles({file("网格 multi.mkv"), file("长片 A.mp4")}, true);
        report.require(ids.size() == 2, "queue populated but must not persist");
        waitMedia(ids[0], player.generation());
        report.wait([&] { return starts.value(player.generation()) == QList{Start::SkipIntro} && !player.paused(); },
                    "the initial intro seek finished before pausing");
        player.setPaused(true);
        report.wait([&] { return player.paused(); }, "pause confirmed");
        trigger(Actions::SortDurationDesc);
        report.wait([&] { return queue.appliedSort() == PlaylistSort{SortField::Duration, SortDirection::Descending}; }, "descending sort applied");
        trigger(Actions::RotateNext);
        trigger(Actions::Scale150);
        report.wait([&] { return player.paused() && player.videoRotation() == 90 && video.canCaptureTransform(player.videoTransformVersion()); },
                    "restorable rotation and video ratio settled");
        video.restoreView(2.25, {.38, .61});
        host.restore(true, true, 365);
        host.window()->setGeometry(980, 210, 380, 430);
        window.showMaximized();
        QTest::qWait(250);
        report.require(window.close(), "normal close flushed pending settings");

        QFile ini(settings.fileName());
        report.require(ini.open(QIODevice::ReadOnly), "persisted profile readable");
        const auto bytes = ini.readAll();
        report.require(!bytes.contains("multi.mkv") && !bytes.contains(".mp4") &&
                       !QFile::exists(profile + "/data/history.json"), "history off left no history or queue paths");
    }

    void read()
    {
        const auto p = session.preferences();
        report.require(p.seekSmallSeconds == 7 && p.seekMediumSeconds == 27 && p.seekLargeSeconds == 71
                       && actions.get(Actions::ForwardSmall)->text().contains("7")
                       && p.skipEnabled && p.introEnabled && p.outroEnabled && p.introSeconds == 4 && p.outroSeconds == 6
                       && !p.recordHistory && p.resumePolicy == 1, "all seek, skip and history fields restored");
        report.require(queue.repeatMode() == RepeatMode::All && queue.shuffleEnabled() &&
                       queue.lastSort() == PlaylistSort{SortField::Duration, SortDirection::Descending}
                       && !queue.appliedSort() && !queue.model().rowCount() && player.path().isEmpty(),
                       "repeat, shuffle and sort restored with empty queue and no open file");
        report.require(closeTo(p.speed, 1.75) && !p.pitchCorrection && closeTo(player.volumeTarget(), 37) && player.muteTarget()
                       && !p.subtitleVisible && closeTo(p.subtitleScale, 1.4) && closeTo(p.audioDelay, .145, .001)
                       && closeTo(p.subtitleDelay, -.225, .001) && p.audioLanguages == "eng" && p.subtitleLanguages == "eng",
                       "all playback, audio and subtitle fields restored");
        report.require(p.userRotationDeg == 90 && p.resetViewOnOpen && closeTo(video.zoomScale(), 2.25)
                       && (video.viewFocus() - QPointF(.38, .61)).manhattanLength() < .02
                       && window.sizePolicy() == WindowSizePolicy::VideoScale && window.lastVideoScale() == 1.5,
                       "additional angle, normalized content point, reset flag, policy and ratio restored");
        report.details["windowGeometry"] = QJsonArray{p.mainGeometry.x(), p.mainGeometry.y(), p.mainGeometry.width(), p.mainGeometry.height()};
        report.require(p.mainGeometry.isValid() && window.windowedGeometry() == p.mainGeometry &&
                       p.maximized && window.isMaximized() && window.windowFlags().testFlag(Qt::WindowStaysOnTopHint)
                       && host.detached() && host.visible() && host.dockWidth() == 365
                       && host.window()->geometry() == QRect(980, 210, 380, 430) && p.playlistGeometry == host.window()->geometry(),
                       "main geometry, maximized, topmost and detached playlist geometry/visibility/width restored");
        report.require(actions.get(Actions::StandardMode)->isChecked() &&
                       actions.get(Actions::MediaInfo)->shortcut().toString(QKeySequence::PortableText) == "Ctrl+Alt+F8"
                       && window.findChild<CaptureService *>()->directory() == output.absoluteFilePath("captures"),
                       "input mode, shortcut and capture directory restored");

        const auto ids = queue.addFiles({file("audio.m4a"), file("网格 multi.mkv"), file("换片 C.mkv"), file("轨道重排.mkv")}, true);
        report.require(ids.size() == 4, "audio and differently numbered video tracks added");
        waitMedia(ids[0], player.generation(), false);
        const auto fromAudio = player.generation();
        report.require(!actions.get(Actions::Scale100)->isEnabled(), "video scaling disabled on audio-only media");
        queue.play(ids[1]);
        waitMedia(ids[1], fromAudio + 1);
        report.wait([&] { return language("audio") == "eng" && language("sub") == "eng" &&
                          player.videoRotation() == 90 && closeTo(video.zoomScale(), 1); }, "next video uses language preferences and resets only the view");
        report.require(player.mediaProperty("aid").toInt() == 1 && player.mediaProperty("sid").toInt() == 1, "first video's English track IDs are 1");
        player.selectTrack("audio", 2, player.generation());
        player.selectTrack("sub", 2, player.generation());
        report.wait([&] { return language("audio") == "jpn" && language("sub") == "jpn"; }, "selected per-file Japanese tracks");
        queue.play(ids[2]);
        waitMedia(ids[2], fromAudio + 2);
        report.wait([&] { return language("audio") == "eng" && language("sub") == "eng" &&
                          player.mediaProperty("aid").toInt() == 1 && player.mediaProperty("sid").toInt() == 1; },
                    "manual per-file track selection did not carry to the next file");
        queue.play(ids[3]);
        waitMedia(ids[3], fromAudio + 3);
        report.wait([&] { return language("audio") == "eng" && language("sub") == "eng" &&
                          player.mediaProperty("aid").toInt() == 2 && player.mediaProperty("sid").toInt() == 2; },
                    "next file resolves languages and does not carry track selections");
        report.require(closeTo(player.propertyTarget("speed").toDouble(), 1.75) && !player.propertyTarget("sub-visibility").toBool()
                       && !QFile::exists(profile + "/data/history.json"), "runtime preferences survived across four media opens with history off");
    }

    void stress()
    {
        auto p = session.preferences();
        p.skipEnabled = p.introEnabled = p.outroEnabled = true; p.introSeconds = 4; p.outroSeconds = 6;
        report.require(session.applyPreferences(p), "mixed stress enables both skips");
        const auto ids = queue.addFiles({file("长片 A.mp4"), file("长片 B.mp4")}, true);
        report.require(ids.size() == 2, "two long media for mixed stress");
        player.setVolume(5);
        waitMedia(ids[0], player.generation());
        QJsonArray samples;
        QSet<GLuint> framebuffers;
        auto *const context = video.context();
        const auto frameSwapped = QObject::connect(&video, &VideoWidget::frameSwapped, &window, [&] {
            if (video.context() == context && video.defaultFramebufferObject()) framebuffers.insert(video.defaultFramebufferObject());
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(frameSwapped); });
        constexpr Actions::Id scales[] = {Actions::Scale50, Actions::Scale100, Actions::Scale150, Actions::Scale200};
        for (int i = 0; i < 32; ++i) {
            host.setVisible(true, false);
            host.setDetached(i % 2 == 0);
            trigger(Actions::Fullscreen);
            report.require(window.isFullScreen(), "entered fullscreen");
            trigger(scales[i % 4]);
            report.wait([&] { return !window.isFullScreen() && window.lastVideoScale() == .5 * (i % 4 + 1); }, "ratio exits fullscreen to a normal window");
            const int angle = (i % 4 + 1) * 90 % 360;
            trigger(Actions::RotateNext);
            const auto prior = player.generation();
            const auto next = ids[i % 2];
            queue.play(ids[(i + 1) % 2]); queue.play(next);
            waitMedia(next, prior + 2);
            report.wait([&] { return starts.value(player.generation()) == QList{Start::SkipIntro}
                              && player.videoRotation() == angle && video.canCaptureTransform(player.videoTransformVersion()); },
                        "latest cut played with one intro and requested angle");
            report.require(completions.value(prior + 1).isEmpty() && video.context() == context && context->isValid() &&
                           video.defaultFramebufferObject() != 0 && window.videoContextValid(), "no stale completion; viewport has a live framebuffer");
            if (i % 4 == 3) {
                player.setPaused(true);
                report.wait([&] { return player.paused(); }, "paused checkpoint");
                QTest::qWait(150);
                const auto image = video.grabFramebuffer();
                report.require(!image.isNull() && image.pixelColor(image.rect().center()) != Qt::black, "real viewport frame readback");
                auto sample = resources();
                video.makeCurrent();
                int live = 0;
                for (const auto name : framebuffers) live += context->functions()->glIsFramebuffer(name) == GL_TRUE;
                video.doneCurrent();
                report.require(live == 1, "only the current Qt viewport framebuffer is still live");
                sample["round"] = i + 1;
                sample["width"] = image.width();
                sample["height"] = image.height();
                sample["framebufferNamesSeen"] = framebuffers.size();
                sample["liveFramebuffers"] = live;
                samples.append(sample);
                report.details["checkpointResources"] = samples;
            }
        }
        const auto early = samples[2].toObject(), late = samples.last().toObject();
        report.require(late["gdi"].toInt() <= early["gdi"].toInt() + 2 &&
                       late["user"].toInt() <= early["user"].toInt() + 2 &&
                       late["handles"].toInt() <= early["handles"].toInt() + 12 &&
                       late["privateMiB"].toDouble() <= early["privateMiB"].toDouble() + 32,
                       "resources bounded after two warm-up checkpoints");
        rebuildContext();
    }

private:
    QString file(const char *name) const { return fixtures.absoluteFilePath(QString::fromUtf8(name)); }
    void trigger(Actions::Id id)
    {
        auto *action = actions.get(id);
        if (!action->isEnabled()) throw std::runtime_error(("disabled action: " + action->objectName()).toStdString());
        action->trigger();
    }
    void waitMedia(quint64 item, quint64 generation, bool hasVideo = true)
    {
        report.wait([&] { return player.generation() == generation && player.itemId() == item &&
                          player.state() == State::Loaded && player.canSeek() &&
                          (!hasVideo || video.canCaptureTransform(player.videoTransformVersion())); },
                    QString("latest item %1, generation %2 loaded").arg(item).arg(generation));
    }
    void crossOutro()
    {
        player.setPaused(true);
        report.wait([&] { return player.paused() && player.canSeek(); }, "pause before outro boundary");
        const auto target = player.duration() - 7.1;
        player.seek(target);
        report.wait([&] { return closeTo(player.position(), target, .18); }, "seek landed before outro boundary");
        player.setMediaProperty("speed", 3);
        player.setPaused(false);
    }
    void rebuildContext()
    {
        trigger(Actions::RotateNext);
        report.wait([&] { return player.paused() && player.videoRotation() == 90 &&
                          video.canCaptureTransform(player.videoTransformVersion()); }, "paused rotated frame before rebuilding OpenGL");
        video.restoreView(2, {.43, .54});
        const auto id = player.playbackIdentity();
        const auto position = player.position();
        const auto focus = video.viewFocus();
        auto *parent = video.parentWidget();
        const auto geometry = video.geometry();
        QWidget otherWindow;
        otherWindow.resize(geometry.size());
        const auto returnVideo = qScopeGuard([&] {
            if (video.parentWidget() == parent) return;
            video.setParent(parent);
            video.setGeometry(geometry);
            video.show();
        });
        QSignalSpy restored(&player, &Player::restored);
        QSignalSpy firstDestroyed(video.context(), &QOpenGLContext::aboutToBeDestroyed);
        video.setParent(&otherWindow);
        video.setGeometry(otherWindow.rect());
        otherWindow.show();
        video.show();
        const auto stable = [&] {
            return player.paused() && player.generation() == id.generation && player.itemId() == id.itemId &&
                closeTo(player.position(), position, .1) && player.videoRotation() == 90 &&
                video.canCaptureTransform(player.videoTransformVersion()) && video.zoomScale() == 2 &&
                (video.viewFocus() - focus).manhattanLength() < .02;
        };
        report.wait([&] { return firstDestroyed.size() == 1 && restored.size() == 1 && stable(); },
                    "new top-level window destroys GL context and restores the exact playback");
        QSignalSpy secondDestroyed(video.context(), &QOpenGLContext::aboutToBeDestroyed);
        video.setParent(parent);
        video.setGeometry(geometry);
        video.show();
        otherWindow.close();
        report.wait([&] { return secondDestroyed.size() == 1 && restored.size() == 2 && stable(); },
                    "original window recreates the GL context again and resumes the paused frame");
        report.require(player.renderEpoch() > id.renderEpoch && starts.value(id.generation) == QList{Start::SkipIntro}
                       && completions.value(id.generation).isEmpty(), "GL rebuilds do not replay the intro or advance the playlist");
    }
    QString language(const QString &type) const
    {
        const auto id = player.mediaProperty(type == "audio" ? "aid" : "sid").toLongLong();
        for (const auto &track : player.tracks())
            if (track.type == type && track.id == id) return track.language;
        return {};
    }

    QDir fixtures, output;
    QString profile;
    Report &report;
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController session;
    PlaylistController &queue;
    Actions &actions;
    PlaylistHost &host;
    VideoWidget &video;
    QHash<quint64, QList<Start>> starts;
    QHash<quint64, QList<Reason>> completions;
};
}

int main(int argc, char **argv)
{
    if (argc == 2 && QByteArrayView(argv[1]) == MetadataProbe::workerArgument) return MetadataProbe::runWorker();
    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments();
    if (args.size() != 4) return 2;
    const auto mode = args[1];
    const QDir output(args[3]);
    if (!output.exists()) return 2;
    Report report;
    bool passed = false;
    try {
        const auto profile = mode == "write" || mode == "read" ? "restart" : mode;
        Suite suite(QDir(args[2]), output, output.absoluteFilePath(profile), report);
        if (mode == "integration") suite.combination();
        else if (mode == "write") suite.write();
        else if (mode == "read") suite.read();
        else if (mode == "stress") suite.stress();
        else return 2;
        passed = true;
    } catch (const std::exception &error) {
        report.details["error"] = QString::fromUtf8(error.what());
    }
    QFile result(output.filePath(mode + ".json"));
    if (!result.open(QIODevice::WriteOnly)) return 2;
    result.write(QJsonDocument(QJsonObject{{"passed", passed}, {"qt", qVersion()}, {"checks", report.checks},
                                          {"details", report.details}}).toJson());
    return passed ? 0 : 1;
}
