#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTimer>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
const QString captureDirectoryName = QStringLiteral("截图 验证");

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < .00001;
}

void require(bool condition, const char *description)
{
    if (!condition) throw std::runtime_error(description);
    std::printf("PASS %s\n", description);
}

bool until(auto predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout) QTest::qWait(10);
    return predicate();
}

bool sameFrame(const QImage &actual, const QString &referencePath)
{
    const QImage expected(referencePath);
    if (actual.isNull() || actual.size() != expected.size()) return false;
    for (int y = actual.height() / 4; y < actual.height() * 3 / 4; y += 23) {
        for (int x = actual.width() / 4; x < actual.width() * 3 / 4; x += 23) {
            const auto a = actual.pixelColor(x, y), b = expected.pixelColor(x, y);
            if (std::abs(a.red() - b.red()) > 2 || std::abs(a.green() - b.green()) > 2
                || std::abs(a.blue() - b.blue()) > 2) return false;
        }
    }
    return true;
}

MainWindow makeWindow(Player &player)
{
    require(player.initialize(), "initialize libmpv");
    return MainWindow(player);
}

struct Probe {
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController session;
    VideoWidget *video = window.findChild<VideoWidget *>();
    PlayerActions *actions = window.findChild<PlayerActions *>();
    PlaylistController *playlist = window.findChild<PlaylistController *>();
    QDir media;
    QDir profile;

    Probe(const QString &profile, const QString &fixtures)
        : settings(profile + "/config"), history(profile + "/data"), window(makeWindow(player)),
          session(player, window, settings, history), media(fixtures), profile(profile)
    {
        window.show();
        require(until([&] { return video->rendererReady(); }), "OpenGL widget is ready");
        std::printf("Qt %s, DPR %.2f, mpv %s\n", qVersion(), video->devicePixelRatioF(),
                    qPrintable(engineProperty("mpv-version").toString()));
    }

    QVariant engineProperty(const char *name) const
    {
        mpv_node node{};
        if (mpv_get_property(player.handle(), name, MPV_FORMAT_NODE, &node) < 0) return {};
        auto result = nodeValue(node);
        mpv_free_node_contents(&node);
        return result;
    }

    bool playbackMatches() const
    {
        return near(engineProperty("speed").toDouble(), 1.5)
            && engineProperty("audio-pitch-correction") == QVariant(false)
            && engineProperty("sub-visibility") == QVariant(false)
            && near(engineProperty("audio-delay").toDouble(), 3599.999)
            && near(engineProperty("sub-delay").toDouble(), -.375);
    }

    void open(const QString &file, bool hasVideo = true)
    {
        const auto path = media.absoluteFilePath(file);
        require(!playlist->addFiles({path}, true).isEmpty(), "open owned fixture from playlist");
        require(until([&] { return player.path() == path && player.state() == Player::State::Loaded
            && !player.tracks().isEmpty() && (!hasVideo || video->canCapture()); }), "fixture loaded");
        player.setPaused(true);
        require(until([&] { return player.paused(); }), "fixture paused");
    }

    void finish()
    {
        require(window.close(), "normal close saves preferences");
        history.waitForSaved();
        player.shutdown();
    }

    void write()
    {
        require(!session.preferences().recordHistory && playlist->model().rowCount() == 0, "history off and queue empty by default");
        const auto &initial = session.preferences();
        require(initial.speed == 1 && initial.pitchCorrection && initial.subtitleVisible && initial.audioDelay == 0
            && initial.subtitleDelay == 0 && !initial.alwaysOnTop && initial.viewScale == 1 && initial.viewFocus == QPointF(.5, .5), "legacy INI loads the defaults for missing fields");
        require(initial.shortcuts.value("play.toggle").toStringList() == QStringList{"Ctrl+P"}, "legacy shortcut override is loaded");
        player.setAudioPreferences(37, true);
        open(QStringLiteral("网格 multi.mkv"));
        require(until([&] { return actions->get(PlayerActions::SubToggle)->isEnabled(); }), "media actions enabled");
        actions->get(PlayerActions::Rate150)->trigger();
        actions->get(PlayerActions::Pitch)->trigger();
        actions->get(PlayerActions::SubToggle)->trigger();
        actions->get(PlayerActions::AlwaysOnTop)->trigger();
        window.findChild<QDoubleSpinBox *>("audioDelaySpin")->setValue(3600);
        require(until([&] { return near(engineProperty("audio-delay").toDouble(), 3600); }), "set upper delay limit");
        window.findChild<QDoubleSpinBox *>("audioDelaySpin")->setValue(3599.999);
        window.findChild<QDoubleSpinBox *>("subDelaySpin")->setValue(-.375);
        video->zoomSteps(100);
        require(until([&] { return playbackMatches() && video->zoomScale() == 8; }), "menu changes confirmed by libmpv and zoom reaches 8x");
        require(until([&] { return near(settings.load().audioDelay, 3599.999); }), "millisecond delay change is saved while open");

        const QPoint from = video->rect().center();
        const QPoint to = from + QPoint(100, 70);
        QTest::mousePress(video, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, video->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(video, &move);
        QTest::mouseRelease(video, Qt::LeftButton, Qt::NoModifier, to);
        require((video->viewFocus() - QPointF(.5, .5)).manhattanLength() > .01, "drag changes the normalized focus");
        require(until([&] { return (settings.load().viewFocus - video->viewFocus()).manhattanLength() < .000001; }), "pan alone triggers the merged save");

        // Apply another setting while a live menu preference changes.
        const auto directory = QFileInfo(settings.fileName()).absoluteDir().filePath(captureDirectoryName);
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            window.findChild<QDoubleSpinBox *>("subScaleSpin")->setValue(1.2);
            dialog->findChild<QLineEdit *>("captureDirectoryEdit")->setText(directory);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
            dialog->reject();
        });
        session.showSettings();
        require(until([&] { return settings.load().captureDirectory == directory
            && playbackMatches() && near(engineProperty("sub-scale").toDouble(), 1.2); }), "applying the dialog retains current playback preferences");
        const double appliedScale = engineProperty("sub-scale").toDouble();
        require(until([&] { return settings.load().subtitleScale == appliedScale; }), "confirmed subtitle scale is saved");
        const auto savedAt = QFileInfo(settings.fileName()).lastModified();
        player.seek(.8);
        require(until([&] { return near(player.position(), .8); }), "seek changes playback position");
        QTest::qWait(900);
        require(QFileInfo(settings.fileName()).lastModified() == savedAt, "playback position updates do not rewrite settings");

        require(video->grabFramebuffer().save(profile.filePath("view-before.png")), "save paused grid reference frame");
        open("audio.m4a", false);
        require(playbackMatches() && video->zoomScale() == 8 && !video->canCapture(), "audio-only load retains playback and view preferences");
        finish();
        const auto saved = settings.load();
        require(saved.speed == 1.5 && !saved.pitchCorrection && !saved.subtitleVisible && saved.alwaysOnTop
            && near(saved.audioDelay, 3599.999) && near(saved.subtitleDelay, -.375) && saved.viewScale == 8
            && (saved.viewFocus - QPointF(.5, .5)).manhattanLength() > .01, "all new values saved with history off");
    }

    void read()
    {
        const auto saved = settings.load();
        require(player.path().isEmpty() && playlist->model().rowCount() == 0 && !saved.recordHistory
            && history.entries().isEmpty(), "startup leaves the queue and history empty");
        require(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint)
            && actions->get(PlayerActions::AlwaysOnTop)->isChecked(), "startup restores native topmost and menu state");
        require(until([&] { return playbackMatches() && engineProperty("mute").toBool()
            && near(engineProperty("volume").toDouble(), 37) && near(engineProperty("sub-scale").toDouble(), 1.2); }), "startup applies saved preferences to idle libmpv");

        for (const auto &file : {QStringLiteral("网格 multi.mkv"), QStringLiteral("portrait.mp4"), QStringLiteral("motion-1080.mp4")}) {
            open(file);
            require(playbackMatches() && video->zoomScale() == 8 && (video->viewFocus() - saved.viewFocus).manhattanLength() < .000001
                && window.findChild<QLabel *>("zoomLabel")->isVisible(), "each video applies the saved view and playback preferences");
            if (file == QStringLiteral("网格 multi.mkv")) {
                const auto restored = video->grabFramebuffer();
                require(restored.save(profile.filePath("view-after.png")), "save restored paused grid frame");
                require(sameFrame(restored, profile.filePath("view-before.png")), "restored paused grid matches the saved framing");
            } else if (file == "portrait.mp4") {
                require(video->grabFramebuffer().save(profile.filePath("view-portrait.png")), "save framing on rotated media");
            }
        }

        window.resize(840, 560);
        require(video->viewFocus() == saved.viewFocus, "resize keeps the requested content point");
        const auto generation = player.generation();
        const auto epoch = player.renderEpoch();
        player.setRendererReady(false);
        player.setRendererReady(true);
        require(until([&] { return player.lastLoadWasRecovery() && !player.recovering() && player.state() == Player::State::Loaded; }), "renderer reload completed");
        require(player.generation() == generation && player.renderEpoch() > epoch && player.paused()
            && playbackMatches() && video->zoomScale() == 8 && video->viewFocus() == saved.viewFocus, "renderer reload preserves playback and view");

        open(QStringLiteral("网格 multi.mkv"));
        require(until([&] { return actions->get(PlayerActions::SubDelayReset)->isEnabled(); }), "delay reset actions enabled");
        actions->get(PlayerActions::AudioDelayReset)->trigger();
        actions->get(PlayerActions::SubDelayReset)->trigger();
        actions->get(PlayerActions::RateReset)->trigger();
        actions->get(PlayerActions::ResetView)->trigger();
        finish();

        const auto reset = settings.load();
        require(reset.audioDelay == 0 && reset.subtitleDelay == 0 && reset.speed == 1 && reset.viewScale == 1
            && reset.viewFocus == QPointF(.5, .5), "immediate normal close saves explicit reset commands");
        require(history.entries().isEmpty() && !QFile::exists(history.fileName()), "history remains off with no media records on disk");
        QFile ini(settings.fileName());
        require(ini.open(QIODevice::ReadOnly), "open saved INI for privacy check");
        const auto bytes = ini.readAll();
        require(!bytes.contains(".mkv") && !bytes.contains(".mp4") && !bytes.contains(".m4a"), "settings contain no fixture paths");
        const QSettings raw(settings.fileName(), QSettings::IniFormat);
        require(raw.value("future/preserved") == QVariant("sentinel") && reset.shortcuts.value("play.toggle").toStringList() == QStringList{"Ctrl+P"}, "unrelated INI keys and legacy shortcuts survive preference saves");
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    std::setlocale(LC_NUMERIC, "C");
    const auto args = app.arguments();
    if (args.size() != 4 || (args[1] != "write" && args[1] != "read")) {
        std::fputs("usage: zopana_playback_preferences write|read fixture-directory profile-directory\n", stderr);
        return 2;
    }
    try {
        if (args[1] == "write") {
            require(!QFile::exists(args[3] + "/config/settings.ini") && QDir().mkpath(args[3] + "/config"), "create an isolated profile");
            QSettings legacy(args[3] + "/config/settings.ini", QSettings::IniFormat);
            legacy.setValue("schemaVersion", 1);
            legacy.setValue("future/preserved", "sentinel");
            legacy.setValue("shortcuts/play.toggle", QStringList{"Ctrl+P"});
            legacy.sync();
            require(legacy.status() == QSettings::NoError, "seed the legacy profile");
        }
        Probe probe(args[3], args[2]);
        if (args[1] == "write") probe.write();
        else probe.read();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
