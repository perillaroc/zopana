#include "captureservice.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "playlisthost.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTest>
#include <QThread>

#include <clocale>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace {
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

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("read probe file");
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size()) throw std::runtime_error("write probe file");
}

void setKeys(const SettingsStore &store, const QVariantMap &keys)
{
    if (!QDir().mkpath(QFileInfo(store.fileName()).absolutePath())) throw std::runtime_error("create profile");
    QSettings ini(store.fileName(), QSettings::IniFormat);
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) ini.setValue(it.key(), it.value());
    ini.sync();
    if (ini.status() != QSettings::NoError) throw std::runtime_error("seed INI keys");
}

qlonglong schema(const SettingsStore &store)
{
    return QSettings(store.fileName(), QSettings::IniFormat).value("schemaVersion").toLongLong();
}

void stores(const QDir &output)
{
    SettingsStore store(output.filePath("legacy"));
    setKeys(store, {{"schemaVersion",1}, {"audio/volume",35}, {"playback/speed",1.5}, {"tracks/audioDelay",.25},
        {"window/alwaysOnTop",true}, {"history/resumePolicy","1.5"}, {"playlist/dockWidth","360.25"},
        {"playlist/order","random"}, {"future/value",QStringList{"one","two"}},
        {"shortcuts/play.toggle",QStringList{"Ctrl+P"}}, {"shortcuts/play.stop",QStringList{}},
        {"shortcuts/future/nested",QRect(1,2,3,4)}});
    auto prefs = store.load();
    require(store.needsWrite() && schema(store) == 1 && prefs.volume == 35 && prefs.speed == 1.5
        && prefs.audioDelay == .25 && prefs.alwaysOnTop, "schema 1 is read without premature writes");
    require(prefs.seekSmallSeconds == 5 && prefs.seekMediumSeconds == 30 && prefs.seekLargeSeconds == 60
        && prefs.repeatMode == RepeatMode::None && !prefs.shuffleEnabled && prefs.resumePolicy == 0 && prefs.dockWidth == 320,
        "missing new fields and fractional integers fall back to defaults");
    require(store.save(prefs) && !store.needsWrite() && schema(store) == 2, "first successful save migrates schema 1 to 2");

    prefs.seekSmallSeconds = 7;
    prefs.seekMediumSeconds = 3600;
    prefs.seekLargeSeconds = 1;
    prefs.skipEnabled = prefs.introEnabled = prefs.outroEnabled = true;
    prefs.introSeconds = 10;
    prefs.outroSeconds = 86400;
    prefs.repeatMode = RepeatMode::All;
    prefs.shuffleEnabled = true;
    prefs.sortField = SortField::Duration;
    prefs.sortDirection = SortDirection::Descending;
    prefs.userRotationDeg = 270;
    prefs.resetViewOnOpen = true;
    prefs.sizePolicy = WindowSizePolicy::VideoScale;
    prefs.lastVideoScale = 1.5;
    require(store.save(prefs), "save new fields and valid limits");
    auto reloaded = store.load();
    reloaded.shortcuts.clear();
    prefs.shortcuts.clear();
    require(reloaded == prefs, "new fields round-trip with typed values and valid limits");
    QSettings raw(store.fileName(), QSettings::IniFormat);
    require(raw.value("playlist/order").toString() == "random" && raw.value("future/value").toStringList() == QStringList{"one","two"}
        && raw.value("shortcuts/future/nested").toRect() == QRect(1,2,3,4)
        && raw.value("shortcuts/play.toggle").toStringList() == QStringList{"Ctrl+P"}
        && raw.contains("shortcuts/play.stop") && raw.value("shortcuts/play.stop").toStringList().isEmpty(),
        "rewrite preserves unknown groups, nested shortcut data and an explicit unbound shortcut");

    SettingsStore future(output.filePath("future"));
    setKeys(future, {{"schemaVersion",qlonglong{3000000000}}, {"playback/seekSmallSeconds","NaN"}, {"playback/seekMediumSeconds","3601"},
        {"playback/seekLargeSeconds","2.5"}, {"playback/speed","Inf"}, {"playback/pitchCorrection","maybe"},
        {"skip/enabled","True"}, {"skip/introSeconds",-1}, {"skip/outroSeconds",86401},
        {"playback/repeatMode","other"}, {"playback/shuffleEnabled","enabled"}, {"playlist/sortField","customField"},
        {"playlist/sortDirection","sideways"}, {"video/userRotationDeg",45}, {"video/viewScale",9},
        {"video/focusU","NaN"}, {"video/focusV",1.25}, {"window/sizePolicy","unrecognized"}, {"window/lastVideoScale",.75},
        {"tracks/audioDelay",3601}, {"tracks/subtitleDelay",-3600}, {"future/extra","keep"}});
    auto expected = SettingsStore::defaults();
    expected.skipEnabled = true;
    expected.subtitleDelay = -3600;
    const auto valid = future.load();
    require(!future.needsWrite() && valid == expected, "future INI uses defaults for invalid enum, flag, range and numeric fields");
    require(future.save(valid) && schema(future) == 3000000000LL
        && QSettings(future.fileName(), QSettings::IniFormat).value("future/extra").toString() == "keep",
        "future schema version and unknown key survive explicit save");
}

struct Session {
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController controller;
    PlayerActions *actions = window.findChild<PlayerActions *>();

    static Player &initialize(Player &player)
    {
        if (!player.initialize()) throw std::runtime_error("initialize libmpv");
        return player;
    }

    Session(const QString &root)
        : settings(root + "/config"), history(root + "/data"), window(initialize(player)), controller(player, window, settings, history)
    {
        window.show();
        auto *video = window.findChild<VideoWidget *>();
        require(until([&] { return video->rendererReady(); }), "settings probe window initializes");
    }

    void edit(auto operation)
    {
        std::exception_ptr failure;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            if (!dialog) { failure = std::make_exception_ptr(std::runtime_error("settings dialog missing")); return; }
            try {
                operation(*dialog, *dialog->findChild<QDialogButtonBox *>());
            } catch (...) { failure = std::current_exception(); }
            dialog->reject();
        });
        controller.showSettings();
        if (failure) std::rethrow_exception(failure);
    }

    void finish()
    {
        require(window.close(), "settings probe window closes");
        player.shutdown();
    }
};

void ui(const QDir &output, const QString &mediaPath)
{
    const auto root = output.filePath("ui");
    const auto capturePath = root + "/saved-captures";
    const auto capture = capturePath + "/sentinel.png";
    writeFile(capture, "owned screenshot sentinel");
    setKeys(SettingsStore(root+"/config"), {{"schemaVersion",1}, {"capture/directory",capturePath}, {"history/enabled",true},
        {"history/resumePolicy",1}, {"tracks/audioLanguages","eng"}, {"tracks/subtitleScale",1.5}, {"playback/speed",2},
        {"window/normalGeometry",QRect(40,80,900,600)}, {"window/maximized",true}, {"window/alwaysOnTop",true},
        {"playlist/detached",true}, {"video/viewScale",2}, {"audio/volume",37},
        {"shortcuts/play.toggle",QStringList{"Ctrl+O"}}, {"shortcuts/play.previous",QStringList{"F12"}},
        {"shortcuts/window.fullscreen",QStringList{"F12"}},
        {"shortcuts/audio.nextTrack",QStringList{"Tab"}}, {"shortcuts/play.stop",QStringList{}}, {"shortcuts/future.command",QStringList{"Ctrl+F12"}}});
    Session s(root);
    require(until([&] { return schema(s.settings) == 2; }), "open application migrates an existing INI after the merge interval");
    require(s.actions->get(PlayerActions::PlayPause)->shortcut() == QKeySequence("Ctrl+O")
        && !s.actions->get(PlayerActions::Open)->shortcuts().contains(QKeySequence("Ctrl+O"))
        && s.actions->get(PlayerActions::Previous)->shortcut() == QKeySequence("F12")
        && s.actions->get(PlayerActions::Fullscreen)->shortcut() == QKeySequence("F11")
        && s.actions->get(PlayerActions::AudioNext)->shortcut() == QKeySequence("J")
        && s.actions->get(PlayerActions::Stop)->shortcuts().isEmpty(),
        "existing custom binding wins a default conflict and fixed keys stay protected");
    auto collision = s.controller.preferences().shortcuts;
    collision.insert("play.toggle", QStringList{"I"});
    const auto ignored = s.actions->applyBindings(collision);
    require(s.actions->get(PlayerActions::PlayPause)->shortcut() == QKeySequence("I")
        && s.actions->get(PlayerActions::MediaInfo)->shortcuts().isEmpty() && ignored.contains("window.mediaInfo")
        && s.actions->get(PlayerActions::QueuePlay)->shortcuts().contains(QKeySequence(Qt::Key_Return)),
        "a conflicting single default stays unbound and queue Enter remains available");
    s.actions->applyBindings(s.controller.preferences().shortcuts);
    require(s.window.isMaximized(), "legacy maximized state is restored");
    s.history.remember(mediaPath, 4, 16);
    s.history.waitForSaved();
    const auto recorded = readFile(s.history.fileName());

    s.edit([&](QDialog &, QDialogButtonBox &buttons) { buttons.button(QDialogButtonBox::RestoreDefaults)->click(); });
    require(s.settings.load().captureDirectory == capturePath && s.settings.load().recordHistory, "cancelling a reset leaves disk untouched");
    s.edit([&](QDialog &, QDialogButtonBox &buttons) {
        buttons.button(QDialogButtonBox::RestoreDefaults)->click();
        buttons.button(QDialogButtonBox::Apply)->click();
    });
    const auto general = s.settings.load();
    require(general.captureDirectory == SettingsStore::defaults().captureDirectory && general.audioLanguages.isEmpty()
        && !general.recordHistory && general.resumePolicy == 0 && general.subtitleScale == 1.5 && general.speed == 2
        && general.shortcuts.value("play.toggle").toStringList() == QStringList{"Ctrl+O"} && readFile(s.history.fileName()) == recorded,
        "general page reset affects only displayed settings and preserves history and bindings");

    s.edit([&](QDialog &dialog, QDialogButtonBox &buttons) {
        auto live = s.controller.preferences();
        live.recordHistory = true;
        live.audioLanguages = "jpn";
        live.shortcuts["file.open"] = QStringList{"Ctrl+F9"};
        require(s.controller.applyPreferences(live), "change preferences while a dialog is open");
        dialog.findChild<QLineEdit *>("captureDirectoryEdit")->setText(capturePath);
        s.player.setMediaProperty("speed", 1.75);
        buttons.button(QDialogButtonBox::Apply)->click();
    });
    auto merged = s.settings.load();
    require(merged.recordHistory && merged.audioLanguages == "jpn" && merged.captureDirectory == capturePath
        && merged.speed == 1.75 && merged.shortcuts.value("file.open").toStringList() == QStringList{"Ctrl+F9"},
        "apply merges only edited fields into current playback and dialog preferences");

    s.edit([&](QDialog &dialog, QDialogButtonBox &buttons) {
        dialog.findChild<QTabWidget *>("settingsTabs")->setCurrentIndex(1);
        buttons.button(QDialogButtonBox::RestoreDefaults)->click();
        buttons.button(QDialogButtonBox::Apply)->click();
    });
    const auto keys = s.settings.load();
    merged.shortcuts = {{"future.command",QStringLiteral("Ctrl+F12")}};
    require(keys == merged && s.actions->get(PlayerActions::PlayPause)->shortcut() == QKeySequence(Qt::Key_Space),
        "shortcut page reset affects only known bindings");

    auto custom = s.controller.preferences();
    custom.shortcuts["play.toggle"] = QStringList{"Ctrl+P"};
    require(s.controller.applyPreferences(custom), "restore a custom binding before global reset");

    s.edit([&](QDialog &dialog, QDialogButtonBox &buttons) {
        QTimer::singleShot(0, &dialog, [&] {
            auto *confirm = dialog.findChild<QMessageBox *>();
            if (!confirm) return;
            for (auto *button : confirm->buttons()) {
                if (confirm->buttonRole(button) == QMessageBox::AcceptRole) { button->click(); return; }
            }
        });
        dialog.findChild<QPushButton *>("resetAllSettingsButton")->click();
        buttons.button(QDialogButtonBox::Apply)->click();
    });
    const auto reset = s.settings.load();
    require(reset.speed == 1 && reset.subtitleScale == 1 && reset.volume == 100 && reset.viewScale == 1 && !reset.alwaysOnTop
        && !reset.maximized && !reset.detached && !reset.recordHistory && reset.audioLanguages.isEmpty() && reset.shortcuts == merged.shortcuts
        && s.player.propertyTarget("speed").toDouble() == 1 && s.window.findChild<VideoWidget *>()->zoomScale() == 1
        && !s.window.isMaximized() && !s.window.windowFlags().testFlag(Qt::WindowStaysOnTopHint)
        && s.actions->get(PlayerActions::PlayPause)->shortcut() == QKeySequence(Qt::Key_Space)
        && !s.window.findChild<PlaylistHost *>()->detached(), "confirmed full reset restores preferences and known bindings");
    require(readFile(s.history.fileName()) == recorded && QFile::exists(mediaPath) && QFile::exists(capture), "reset preserves history, media and captures");

    s.controller.saveWindows();
    const auto beforeClear = readFile(s.settings.fileName());
    s.controller.clearHistory();
    s.history.waitForSaved();
    require(s.history.entries().isEmpty() && readFile(s.settings.fileName()) == beforeClear, "clear history changes only history");

    require(QFile::setPermissions(s.settings.fileName(), QFile::ReadOwner), "make owned settings INI read-only");
    s.edit([&](QDialog &dialog, QDialogButtonBox &buttons) {
        dialog.findChild<QLineEdit *>("captureDirectoryEdit")->setText(capturePath);
        buttons.button(QDialogButtonBox::Apply)->click();
        require(dialog.findChild<QLabel *>("settingsMessage")->text().contains(QStringLiteral("保存失败")), "settings dialog reports failed save");
    });
    require(readFile(s.settings.fileName()) == beforeClear && s.window.findChild<CaptureService *>()->directory() == reset.captureDirectory,
        "failed dialog apply preserves the previous file and runtime values");
    require(QFile::setPermissions(s.settings.fileName(), QFile::ReadOwner | QFile::WriteOwner), "restore write access after a failed dialog apply");
    s.player.setMediaProperty("speed", 1.25);
    require(until([&] { return s.settings.load().speed == 1.25; }), "merged save retries on the next change after a failure");
    const auto beforeMenu = readFile(s.settings.fileName());
    require(QFile::setPermissions(s.settings.fileName(), QFile::ReadOwner), "protect saved INI for menu write failure");
    auto *osd = s.window.findChild<QLabel *>("playerOsd");
    osd->hide();
    s.actions->get(PlayerActions::AlwaysOnTop)->trigger();
    require(until([&] { return osd->property("kind").toString() == "storage" && osd->isVisible(); })
        && !s.settings.error().isEmpty(), "read-only file rejects a merged save and shows a warning");
    require(s.window.windowFlags().testFlag(Qt::WindowStaysOnTopHint) && readFile(s.settings.fileName()) == beforeMenu,
        "failed menu save preserves the working preference and previous file");
    require(QFile::setPermissions(s.settings.fileName(), QFile::ReadOwner | QFile::WriteOwner), "restore write access to owned INI");
    s.finish();
    require(s.settings.load().alwaysOnTop && s.settings.load().speed == 1.25, "normal close retries the interrupted save");
}

void crashWrite(const QDir &output)
{
    Session session(output.absolutePath());
    session.player.setMediaProperty("speed", 1.5);
    SettingsStore reader(output.filePath("config"));
    require(until([&] { return reader.load().speed == 1.5; }), "merged save reaches disk");
    QTest::qWait(850);
    session.player.setMediaProperty("speed", 2.5);
    require(reader.load().speed == 1.5, "second change is still inside the merge window");
    std::fflush(stdout);
    writeFile(output.filePath("checkpoint-ready"), "ready");
    // Leave the event loop at this checkpoint until the parent kills the process.
    QThread::msleep(20000);
    throw std::runtime_error("checkpoint process was expected to be terminated");
}

void crashRead(const QDir &output)
{
    Session session(output.absolutePath());
    require(schema(session.settings) == 2 && session.player.propertyTarget("speed").toDouble() == 1.5,
        "restart restores the last atomic commit after force termination");
    session.finish();
}

void protectedDirectory(const QDir &output)
{
    SettingsStore store(output.absolutePath());
    const auto original = readFile(store.fileName());
    auto p = store.load();
    p.speed = 2.5;
    require(!store.save(p) && !store.error().isEmpty() && readFile(store.fileName()) == original,
        "write-protected directory reports failure and preserves the existing INI");
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    std::setlocale(LC_NUMERIC, "C");
    const auto args = app.arguments();
    if (args.size() != 4) return 2;
    const QDir output(args[3]);
    try {
        if (args[1] == "stores") stores(output);
        else if (args[1] == "ui") ui(output, QFileInfo(args[2]).absoluteFilePath());
        else if (args[1] == "protected-directory") protectedDirectory(output);
        else if (args[1] == "crash-write") crashWrite(output);
        else if (args[1] == "crash-read") crashRead(output);
        else return 2;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
