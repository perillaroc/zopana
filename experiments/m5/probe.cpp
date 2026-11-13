#include "mainwindow.h"
#include "player.h"
#include "playeractions.h"
#include "settingsstore.h"
#include "historystore.h"
#include "sessioncontroller.h"
#include "settingsdialog.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "videowidget.h"
#include "captureservice.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QTabWidget>
#include <QMessageBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace {
bool until(const std::function<bool()> &condition,int timeout=8000) { QElapsedTimer timer; timer.start(); while(!condition() && timer.elapsed()<timeout) QTest::qWait(10); return condition(); }
struct Report {
    QString output; QJsonArray checks; double dpr=1;
    explicit Report(QString destination):output(QDir::fromNativeSeparators(QFileInfo(destination).absoluteFilePath())) { QDir().mkpath(output); }
    void check(QString name,bool passed,QJsonObject details={}) { details["name"]=name; details["passed"]=passed; checks.append(details); std::printf("%s %s\n",passed?"PASS":"FAIL",name.toUtf8().constData()); std::fflush(stdout); }
    void require(QString name,bool passed) { check(name,passed); if(!passed) throw std::runtime_error(name.toStdString()); }
    bool save() { bool passed=true; for(const auto &c:checks) passed&=c.toObject()["passed"].toBool(); QFile file(output+"/results.json"); if(!file.open(QIODevice::WriteOnly)) return false; file.write(QJsonDocument(QJsonObject{{"passed",passed},{"devicePixelRatio",dpr},{"checks",checks}}).toJson()); return passed; }
};
void write(const QString &path,const QByteArray &bytes) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size()) throw std::runtime_error("fixture write failed"); }
void stores(Report &r)
{
    QTemporaryDir temporary(r.output+"/stores-XXXXXX"); r.require("temporary data directory",temporary.isValid()); const auto root=temporary.path(); SettingsStore settings(root+"/config");
    const auto defaults=settings.load(); r.check("default preferences and stable app identity",!defaults.recordHistory && defaults.resumePolicy==0 && defaults.volume==100 && defaults.subtitleScale==1 && QCoreApplication::organizationName()=="zopana" && QCoreApplication::applicationName()=="zopana");
    auto p=defaults; p.volume=35; p.muted=true; p.standardMode=true; p.captureDirectory=root+QString::fromUtf8("/中文 截图"); p.mainGeometry=QRect(100,100,900,600); p.detached=true; p.playlistVisible=true; p.playlistGeometry=QRect(1000,150,320,500); p.dockWidth=380; p.shortcuts["future.command"]=QStringList{"Ctrl+F12"}; r.require("INI preferences save atomically",settings.save(p));
    { QSettings raw(settings.fileName(),QSettings::IniFormat); raw.setValue("schemaVersion",9); raw.setValue("future/keep","retained"); raw.setValue("audio/volume","invalid"); raw.setValue("history/resumePolicy",999); raw.sync(); }
    const auto loaded=settings.load(); r.check("invalid values fall back while valid fields survive",loaded.volume==100 && loaded.resumePolicy==0 && loaded.muted && loaded.detached && loaded.standardMode && loaded.mainGeometry==p.mainGeometry && loaded.captureDirectory==p.captureDirectory);
    r.require("save compatible settings",settings.save(loaded)); { QSettings raw(settings.fileName(),QSettings::IniFormat); r.check("unknown INI fields and future schema survive",raw.value("future/keep").toString()=="retained" && raw.value("schemaVersion").toInt()==9 && loaded.shortcuts.contains("future.command")); }
    write(root+"/partial/settings.ini","[audio]\nvolume=42\n[broken\ninvalid\n[input]\nstandardMode=true\n"); SettingsStore partial(root+"/partial"); const auto partialValues=partial.load(); r.check("partially corrupt INI preserves readable values",partialValues.volume==42 && !partial.error().isEmpty()); r.check("partially corrupt INI can be rewritten",partial.save(partialValues));
    const QList<QRect> screens{QRect(0,0,1920,1040),QRect(-1600,0,1600,900)};
    const auto left=SettingsStore::visibleGeometry(QRect(-1500,120,900,600),screens,{640,400}); r.check("negative-coordinate display stays on its screen",left.left()<0 && screens[1].contains(left));
    const auto removed=SettingsStore::visibleGeometry(QRect(6000,-3000,4000,3000),{screens[0]},{640,400}); r.check("removed screen and oversized window clamp visibly",screens[0].contains(removed) && removed.top()>=32);
    const auto missing=SettingsStore::visibleGeometry({},screens,{640,400}); r.check("invalid geometry receives visible default",missing.isValid() && screens[0].contains(missing));
    HistoryStore history(root+"/history"); QSignalSpy saved(&history,&HistoryStore::saved);
    for(int i=0;i<60;++i) { const auto path=root+QString("/media/episode-%1.bin").arg(i); write(path,"immutable test media"); history.remember(path,45,137); }
    history.waitForSaved(); r.check("recent history caps at fifty and writes coalesce",history.entries().size()==50 && saved.count()<=2 && !saved.isEmpty());
    const auto item=history.entries().first(); r.check("valid point requires thirty seconds on both sides",item.resumable()); history.remember(item.path.toUpper(),30,137); r.check("case-insensitive local paths deduplicate",history.entries().size()==50 && !history.find(item.path)->resumable());
    history.remember(item.path,45,75); r.check("near-end point does not resume",!history.find(item.path)->resumable()); history.remember(item.path,45,137); history.waitForSaved();
    { HistoryStore restored(root+"/history"); r.check("JSON round trip preserves metadata and position",restored.entries().size()==50 && restored.find(item.path)->resumable()); }
    write(item.path,"changed size"); r.check("changed media invalidates old breakpoint",!history.find(item.path)->matchesFile() && !history.find(item.path)->resumable());
    QFile::remove(item.path); r.check("missing file remains recent but cannot resume",history.find(item.path).has_value() && !history.find(item.path)->matchesFile());
    const auto end=history.entries()[1].path; history.remember(end,100,137,true); r.check("natural EOF clears only progress",history.find(end)->position==0 && history.entries().size()==50);
    history.clear(); history.waitForSaved(); { HistoryStore restored(root+"/history"); r.check("clear persists empty history",restored.entries().isEmpty()); }
    write(root+"/corrupt/history.json","{bad json"); HistoryStore corrupt(root+"/corrupt"); r.check("corrupt history is ignored with visible error information",corrupt.entries().isEmpty() && !corrupt.loadError().isEmpty());
    write(root+"/blocked","file blocks directory creation"); SettingsStore blocked(root+"/blocked"); r.check("settings destination failure is reported",!blocked.save(defaults) && !blocked.error().isEmpty());
    SettingsStore readonly(root+"/readonly"); r.require("create read-only settings fixture",readonly.save(defaults)); QFile before(readonly.fileName()); if(!before.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read settings fixture"); const auto oldBytes=before.readAll(); before.close();
    r.require("mark owned settings file read-only",QFile::setPermissions(readonly.fileName(),QFile::ReadOwner)); auto changed=defaults; changed.volume=27; const bool denied=!readonly.save(changed); QFile after(readonly.fileName()); if(!after.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read original settings"); const auto newBytes=after.readAll(); after.close(); if(!QFile::setPermissions(readonly.fileName(),QFile::ReadOwner|QFile::WriteOwner)) throw std::runtime_error("cannot restore fixture permissions"); r.check("read-only target preserves prior atomic settings",denied && oldBytes==newBytes);
    HistoryStore failed(root+"/blocked"); QSignalSpy errors(&failed,&HistoryStore::error); failed.remember(end,50,137); failed.flush(); r.require("background history failure returns",until([&] { return !errors.isEmpty(); })); failed.remember(end,55,137); failed.waitForSaved(); r.check("storage failure keeps session state and reports once",failed.find(end)->position==55 && errors.count()==1);
}
class Suite : public Report
{
public:
    QTemporaryDir temporary;
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController session;
    PlayerActions *actions;
    VideoWidget *video;
    QString fixtures,longFile;
    Suite(QString media,QString out):Report(out),temporary(output+"/session-XXXXXX"),settings(temporary.path()+"/config"),history(temporary.path()+"/data"),window(initialize()),session(player,window,settings,history),fixtures(QDir::fromNativeSeparators(QFileInfo(media).absoluteFilePath()))
    {
        video=window.findChild<VideoWidget *>(); actions=window.findChild<PlayerActions *>(); longFile=QDir(fixtures).absoluteFilePath("../zopana-ui-m2-v1/long-grid.mp4"); player.setVolume(0); window.show(); window.raise(); window.activateWindow(); require("main window active",QTest::qWaitForWindowActive(&window)); require("renderer ready",until([&] { return video->rendererReady(); })); dpr=video->devicePixelRatioF(); video->setFocus();
    }
    Player &initialize() { if(!player.initialize()) throw std::runtime_error("engine initialize failed"); return player; }
    ~Suite() { player.shutdown(); }
    void open(const QString &path) { player.openFile(path); require("file loaded",until([&] { return player.state()==Player::State::Loaded && player.seekable() && video->canCapture(); })); }
    void paused(bool value) { player.setPaused(value); require("pause feedback",until([&] { return player.paused()==value; })); }
    void seek(double value) { player.seek(value); require("seek feedback",until([&] { return std::abs(player.position()-value)<.15; })); }
    void shot(const QString &name) { check("screenshot "+name,window.grab().save(output+'/'+name+".png")); }
    void dialog()
    {
        const auto original=session.preferences().captureDirectory; bool seen=false;
        QTimer::singleShot(80,&window,[&] { auto *d=QApplication::activeModalWidget(); if(d) { seen=d->objectName()=="settingsDialog"; d->findChild<QLineEdit *>("captureDirectoryEdit")->setText(temporary.path()+"/cancelled"); qobject_cast<QDialog *>(d)->reject(); } });
        actions->get(PlayerActions::Settings)->trigger(); check("settings cancel leaves runtime and disk unchanged",seen && session.preferences().captureDirectory==original && window.findChild<CaptureService *>()->directory()==original);
        QTimer::singleShot(80,&window,[&] { auto *d=QApplication::activeModalWidget(); if(!d) return; d->findChild<QLineEdit *>("captureDirectoryEdit")->setText(temporary.path()+QString::fromUtf8("/中文 截图")); d->findChild<QLineEdit *>("audioLanguagesEdit")->setText("eng,zho"); d->findChild<QLineEdit *>("subtitleLanguagesEdit")->setText("zho,eng"); auto *box=d->findChild<QDialogButtonBox *>(); box->button(QDialogButtonBox::Apply)->click(); check("screenshot settings-general",d->grab().save(output+"/settings-general.png")); qobject_cast<QDialog *>(d)->reject(); });
        QTest::keyClick(video,Qt::Key_F5); check("F5 settings apply persists after later cancel",settings.load().audioLanguages=="eng,zho" && session.preferences().captureDirectory.contains(QString::fromUtf8("中文 截图")) && window.findChild<CaptureService *>()->directory()==session.preferences().captureDirectory);
        const auto applied=session.preferences().captureDirectory; bool draftDefault=false; QTimer::singleShot(80,&window,[&] { auto *d=QApplication::activeModalWidget(); if(!d) return; d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click(); draftDefault=d->findChild<QLineEdit *>("captureDirectoryEdit")->text()==SettingsStore::defaults().captureDirectory && session.preferences().captureDirectory==applied; qobject_cast<QDialog *>(d)->reject(); }); actions->get(PlayerActions::Settings)->trigger(); check("restore defaults stays in draft until applied",draftDefault && session.preferences().captureDirectory==applied);
    }
    void bindings()
    {
        bool recorded=false,replaced=false;
        QTimer::singleShot(80,&window,[&] { auto *d=QApplication::activeModalWidget(); if(!d) return; d->findChild<QTabWidget *>("settingsTabs")->setCurrentIndex(1); d->findChild<QLineEdit *>("shortcutFilter")->setText("play.toggle"); auto *first=d->findChild<QKeySequenceEdit *>("shortcutFirst"); first->setFocus(); QTest::keyClick(first,Qt::Key_K,Qt::ControlModifier); recorded=first->keySequence()==QKeySequence("Ctrl+K"); first->setKeySequence(QKeySequence("Ctrl+O"));
            QTimer::singleShot(60,d,[&] { auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(box) for(auto *button:box->buttons()) if(button->text()==QStringLiteral("替换原绑定")) { replaced=true; button->click(); break; } });
            d->findChild<QPushButton *>("setBindingButton")->click(); check("screenshot settings-shortcuts",d->grab().save(output+"/settings-shortcuts.png")); d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); qobject_cast<QDialog *>(d)->reject(); });
        actions->get(PlayerActions::Settings)->trigger(); check("shortcut recorder captures one combination",recorded); check("explicit replacement transfers conflicting binding",replaced && actions->get(PlayerActions::PlayPause)->shortcut()==QKeySequence("Ctrl+O") && !actions->get(PlayerActions::Open)->shortcuts().contains(QKeySequence("Ctrl+O")),{{"replaced",replaced},{"playKey",actions->get(PlayerActions::PlayPause)->shortcut().toString()},{"openKey",actions->get(PlayerActions::Open)->shortcut().toString()},{"defaults",QJsonObject::fromVariantMap(actions->defaultBindings())},{"overrides",QJsonObject::fromVariantMap(session.preferences().shortcuts)}});
        auto restored=session.preferences(); restored.shortcuts.remove("play.toggle"); restored.shortcuts.remove("file.open"); require("default map restored after conflict",session.applyPreferences(restored) && actions->get(PlayerActions::Open)->shortcuts().contains(QKeySequence("Ctrl+O")));
        for(const auto &key:QStringList{"Esc","Tab","Shift+Tab","F11","Alt+F4","Ctrl+K, Ctrl+C"}) check("reject reserved or chord binding "+key,!actions->bindingError("play.toggle",{key}).isEmpty());
        check("window scope rejects bare letters and editing shortcuts",!actions->bindingError("file.open",{"A"}).isEmpty() && !actions->bindingError("file.open",{"Ctrl+C"}).isEmpty());
        check("watching and queue bindings are exclusive",!actions->conflicts("play.toggle",{"Delete"},{}).contains("queue.removeSelected"));
        check("normalized punctuation aliases conflict",actions->conflicts("play.toggle",{"Ctrl+Shift+="},{}).contains("sub.scaleUp"));
        check("window and watching bindings overlap",actions->conflicts("play.toggle",{"Ctrl+O"},{}).contains("file.open"));
        auto p=session.preferences(); p.shortcuts["play.toggle"]=QStringList{"Ctrl+P"}; p.shortcuts["future.command"]=QStringList{"Ctrl+F12"}; require("apply custom shortcut",session.applyPreferences(p));
        check("active QAction and tooltip update immediately",actions->get(PlayerActions::PlayPause)->shortcuts()==QList<QKeySequence>{QKeySequence("Ctrl+P")} && actions->get(PlayerActions::PlayPause)->toolTip().contains("Ctrl+P"));
        paused(true); video->setFocus(); QTest::keyClick(video,Qt::Key_P,Qt::ControlModifier); require("custom shortcut plays",until([&] { return !player.paused(); })); paused(true); QTest::keyClick(video,Qt::Key_Space); QTest::qWait(60); check("old binding is removed",player.paused());
        QLineEdit edit(&window); edit.setGeometry(20,60,250,32); edit.show(); edit.raise(); edit.setFocus(); QTest::keyClick(&edit,Qt::Key_P,Qt::ControlModifier); QTest::keyClicks(&edit,"typing"); check("editor keeps native keys and blocks custom watching shortcut",player.paused() && edit.text()=="typing"); edit.hide(); video->setFocus();
        bool help=false; QTimer::singleShot(80,&window,[&] { auto *d=QApplication::activeModalWidget(); if(d) { for(auto *table:d->findChildren<QTableWidget *>()) for(int row=0;row<table->rowCount();++row) for(int c=0;c<table->columnCount();++c) if(table->item(row,c) && table->item(row,c)->text().contains("Ctrl+P")) help=true; qobject_cast<QDialog *>(d)->reject(); } }); actions->get(PlayerActions::Shortcuts)->trigger(); check("shortcut help shows current mapping",help);
        auto invalid=p.shortcuts; invalid["file.open"]=actions->defaultBindings().value("file.open"); invalid["audio.nextTrack"]=QStringList{"Ctrl+O"}; const auto ignored=actions->applyBindings(invalid); check("invalid loaded bindings fall back without losing valid overrides",ignored.contains("audio.nextTrack") && actions->get(PlayerActions::PlayPause)->shortcut()==QKeySequence("Ctrl+P") && actions->get(PlayerActions::AudioNext)->shortcut()==QKeySequence("J"),{{"ignored",QJsonArray::fromStringList(ignored)},{"playKey",actions->get(PlayerActions::PlayPause)->shortcut().toString()},{"audioKey",actions->get(PlayerActions::AudioNext)->shortcut().toString()},{"overrides",QJsonObject::fromVariantMap(invalid)}});
        p.shortcuts.remove("play.toggle"); require("restore default binding",session.applyPreferences(p)); check("default Space and fixed F11 survive reset",actions->get(PlayerActions::PlayPause)->shortcut()==QKeySequence(Qt::Key_Space) && actions->get(PlayerActions::Fullscreen)->shortcuts().contains(QKeySequence(Qt::Key_F11)));
    }
    void resume()
    {
        auto p=session.preferences(); p.recordHistory=true; p.resumePolicy=0; session.applyPreferences(p); history.remember(longFile,45,137); history.waitForSaved(); open(longFile);
        require("valid breakpoint shows paused prompt",until([&] { return session.resumePending() && window.findChild<QWidget *>("resumePrompt")->isVisible() && player.paused(); }));
        check("continue is default keyboard focus",QApplication::focusWidget()==window.findChild<QPushButton *>("resumeContinueButton")); shot("resume-prompt"); QTest::keyClick(window.findChild<QPushButton *>("resumeContinueButton"),Qt::Key_Return); require("continue seeks before playback",until([&] { return !session.resumePending() && !player.paused() && player.position()>=45 && player.position()<47; }));
        paused(true); seek(50); session.flushProgress(); history.waitForSaved(); check("pause and explicit progress flush persist position",std::abs(history.find(longFile)->position-50)<.15);
        open(fixtures+"/grid.mp4"); open(longFile); require("prompt appears on reopening",until([&] { return session.resumePending() && window.findChild<QWidget *>("resumePrompt")->isVisible(); }));
        video->setFocus(); player.seek(12); require("user seek dismisses prompt and honors target",until([&] { return !session.resumePending() && !player.paused() && player.position()>=12 && player.position()<14; }));
        paused(true); seek(45); session.flushProgress(); open(fixtures+"/grid.mp4"); open(longFile); require("prompt before user play",until([&] { return session.resumePending() && player.paused(); })); video->setFocus(); QTest::keyClick(video,Qt::Key_Space); require("user play dismisses prompt from start",until([&] { return !session.resumePending() && !player.paused() && player.position()<3; }));
        paused(true); seek(45); session.flushProgress(); open(fixtures+"/grid.mp4"); open(longFile); require("timeout prompt active",until([&] { return window.findChild<QWidget *>("resumePrompt")->isVisible(); })); require("eight second timeout starts from beginning",until([&] { return !session.resumePending() && !player.paused() && player.position()<3; },9500));
        paused(true); seek(45); session.flushProgress(); player.stop(); history.waitForSaved(); check("stop preserves stored breakpoint",history.find(longFile)->position>=44.8); actions->get(PlayerActions::PlayPause)->trigger(); require("play after stop starts at zero without prompt",until([&] { return player.state()==Player::State::Loaded && !session.resumePending() && !player.paused() && player.position()<3; }));
        paused(true); seek(45); session.flushProgress(); p=session.preferences(); p.resumePolicy=1; session.applyPreferences(p); open(fixtures+"/grid.mp4"); open(longFile); require("always resume applies valid point automatically",until([&] { return !session.resumePending() && !player.paused() && player.position()>=45 && player.position()<48; }));
        paused(true); p=session.preferences(); p.resumePolicy=2; session.applyPreferences(p); open(fixtures+"/grid.mp4"); open(longFile); require("start-over policy ignores saved point",until([&] { return !session.resumePending() && !player.paused() && player.position()<3; }));
    }
    void records()
    {
        auto p=session.preferences(); p.recordHistory=false; session.applyPreferences(p); const auto count=history.entries().size(); open(fixtures+"/portrait.mp4"); paused(true); session.flushProgress(); check("disabled history stops new entries and retains old records",history.entries().size()==count && !history.find(fixtures+"/portrait.mp4"));
        p=session.preferences(); p.recordHistory=true; session.applyPreferences(p); session.flushProgress(); check("reenabled recording can record current media",history.find(fixtures+"/portrait.mp4").has_value());
        session.clearHistory(); QTest::qWait(5300); session.flushProgress(); history.waitForSaved(); check("clear blocks periodic and pause-based recreation",history.entries().isEmpty());
        p=session.preferences(); p.recordHistory=false; session.applyPreferences(p); p.recordHistory=true; session.applyPreferences(p); session.flushProgress(); check("history toggle cannot undo current-file clear suppression",history.entries().isEmpty());
        player.openFile(fixtures+"/portrait.mp4",0,{},true); require("automatic replay loads",until([&] { return player.state()==Player::State::Loaded && video->canCapture(); })); session.flushProgress(); check("automatic replay cannot undo clear suppression",history.entries().isEmpty());
        open(fixtures+"/portrait.mp4"); paused(true); session.flushProgress(); check("explicit reopen releases clear suppression",history.entries().size()==1);
        QStringList recentFixtures;
        for (int i = 0; i < 12; ++i) {
            const auto path = temporary.path() + QString("/recent-%1.mkv").arg(i);
            write(path, "recent fixture");
            recentFixtures.append(QFileInfo(path).canonicalFilePath());
            history.remember(path, 0, 16);
        }
        QTest::qWait(100);
        auto *menu = window.findChild<QMenu *>("recentFilesMenu");
        check("file menu shows only ten recent paths", menu->actions().size() == 10);
        QString removedPath;
        for (const auto *action : menu->actions()) {
            if (recentFixtures.contains(action->toolTip())) {
                removedPath = action->toolTip();
                break;
            }
        }
        require("remove only a temporary recent fixture", !removedPath.isEmpty() && QFile::remove(removedPath));
        QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
        bool missingDisabled = false;
        for (const auto *action : menu->actions()) {
            if (action->toolTip().section('\n', 0, 0) == removedPath) missingDisabled = !action->isEnabled();
        }
        check("missing recent file stays visible and disabled", missingDisabled);
        session.saveWindows(); check("runtime subtitle scale and mode persist",settings.load().subtitleScale==player.propertyTarget("sub-scale").toDouble());
        open(fixtures+QString::fromUtf8("/网格 multi.mkv")); paused(true); const auto selected=player.mediaProperty("aid"); p=session.preferences(); p.audioLanguages="jpn"; p.subtitleLanguages="jpn"; session.applyPreferences(p); QTest::qWait(80); check("language preference preserves current manual selection",player.mediaProperty("aid")==selected);
        open(fixtures+QString::fromUtf8("/网格 multi.mkv")); require("language preference applies to next audio and subtitle tracks",until([&] { bool audio=false,sub=false; for(const auto &track:player.tracks()) { audio|=track.type=="audio" && track.language=="jpn" && track.id==player.mediaProperty("aid").toLongLong(); sub|=track.type=="sub" && track.language=="jpn" && track.id==player.mediaProperty("sid").toLongLong(); } return audio && sub; })); paused(true); shot("m5-player");
        seek(player.lastPosition()); session.flushProgress(); require("EOF test has a stored nonzero position",history.find(player.path()) && history.find(player.path())->position>10); player.setPaused(false); require("natural EOF reached",until([&] { return player.state()==Player::State::Ended; })); check("natural EOF clears saved progress and retains recent item",history.find(player.path()) && history.find(player.path())->position==0);
    }
    void run() { dialog(); open(fixtures+QString::fromUtf8("/网格 multi.mkv")); session.flushProgress(); check("playback is not recorded by default",history.entries().isEmpty()); bindings(); resume(); records(); }
};
void roundtrip(Report &r,const QString &fixtures,bool reading,bool closeInFullscreen)
{
    const QSize normalSize(1000,650);
    SettingsStore settings(r.output+"/persist/config"); HistoryStore history(r.output+"/persist/data");
    if(!reading) { auto p=SettingsStore::defaults(); p.recordHistory=true; p.volume=37; p.muted=true; p.standardMode=true; p.subtitleScale=1.3; p.mainGeometry=QRect(QPoint(60,80),normalSize); p.playlistGeometry=QRect(1100,120,320,480); p.detached=true; p.playlistVisible=true; p.dockWidth=360; p.shortcuts["play.toggle"]=QStringList{"Ctrl+P"}; r.require("seed restart preferences",settings.save(p)); }
    Player player; r.require("restart engine initialized",player.initialize()); MainWindow window(player); SessionController session(player,window,settings,history); window.show(); window.raise(); window.activateWindow(); auto *video=window.findChild<VideoWidget *>(); r.require("restart window ready",QTest::qWaitForWindowActive(&window) && until([&] { return video->rendererReady(); })); r.dpr=video->devicePixelRatioF(); auto *host=window.findChild<PlaylistHost *>(); auto *actions=window.findChild<PlayerActions *>();
    auto *maximize=window.findChild<QToolButton *>("windowMaximizeButton");
    if(reading) {
        r.require("restart audio state restored",until([&] { return player.volumeTarget()==37 && player.muted(); }));
        r.check("restart restores mode and subtitle scale",actions->get(PlayerActions::StandardMode)->isChecked() && std::abs(player.propertyTarget("sub-scale").toDouble()-1.3)<.001);
        r.check("restart restores detached visible list without minimization",host->detached() && host->preferredVisible() && !host->window()->isMinimized());
        r.check("restart does not restore fullscreen or zoom",!window.isFullScreen() && !window.isMinimized() && video->zoomScale()==1 && player.propertyTarget("speed").toDouble()==1);
        r.require("restart opens maximized",window.isMaximized());
        r.check("restart restores shortcut overrides",actions->get(PlayerActions::PlayPause)->shortcut()==QKeySequence("Ctrl+P"));
        const auto *menu=window.findChild<QMenu *>("recentFilesMenu");
        r.require("enabled history remains accessible in the File menu after restart",session.preferences().recordHistory && history.entries().size()>=8 && menu && menu->isEnabled() && menu->actions().size()==history.entries().size());
        auto *card=window.findChild<QWidget *>("stateCard"); bool showsRecent=false;
        for(auto *button:card->findChildren<QToolButton *>()) showsRecent|=button->isVisible() && menu->actions().contains(button->defaultAction());
        r.check("startup card has no recent files with history enabled",card->isVisible() && !showsRecent);
        r.check("restart geometry remains visible",window.screen()->availableGeometry().intersects(window.frameGeometry()) && host->window()->screen()->availableGeometry().intersects(host->window()->frameGeometry()));
        QTest::mouseClick(maximize,Qt::LeftButton);
        r.require("restore button leaves maximized mode",until([&] { return !window.isMaximized(); }));
        QTest::qWait(80);
        r.check("restore button returns to the original window size",window.size()==normalSize,{{"width",window.width()},{"height",window.height()}});
        const auto normal=window.geometry(); window.resize(640,400); QTest::qWait(80); auto *bar=window.findChild<QWidget *>("controlBar"); r.check("startup card fits narrow window above controls",card->y()>=0 && card->geometry().bottom()<bar->y()); window.grab().save(r.output+"/startup-640.png"); window.setGeometry(normal); window.showMaximized();
    } else {
        for(int i=0;i<8;++i) { const auto path=r.output+QString("/persist/recent-%1.mkv").arg(i); write(path,"restart recent fixture"); history.remember(path,0,16); }
        player.openFile(fixtures+"/grid.mp4"); r.require("restart writer media ready",until([&] { return player.state()==Player::State::Loaded && video->canCapture(); })); player.setPaused(true); video->zoomSteps(5); player.setMediaProperty("speed",2.0); host->window()->showMinimized();
        r.check("original window has the configured size",window.size()==normalSize,{{"width",window.width()},{"height",window.height()}});
        QTest::mouseClick(maximize,Qt::LeftButton);
        r.require("writer window maximizes",until([&] { return window.isMaximized(); }));
        actions->get(PlayerActions::Fullscreen)->trigger();
        r.require("writer enters fullscreen",until([&] { return window.isFullScreen(); }));
        if(!closeInFullscreen) {
            QTest::qWait(100);
            session.saveWindows();
            const auto fullscreenSettings=settings.load();
            r.check("fullscreen and minimized list excluded from persisted flags",fullscreenSettings.detached && fullscreenSettings.playlistVisible && fullscreenSettings.maximized);
            r.check("fullscreen preserves the original window size",fullscreenSettings.mainGeometry.size()==normalSize,{{"width",fullscreenSettings.mainGeometry.width()},{"height",fullscreenSettings.mainGeometry.height()}});
            actions->get(PlayerActions::Fullscreen)->trigger();
            r.require("writer exits fullscreen before closing maximized",until([&] { return window.isMaximized() && !window.isFullScreen(); }));
        }
    }
    if(closeInFullscreen) {
        r.require("quit requested while still fullscreen",window.isFullScreen());
        actions->get(PlayerActions::Quit)->trigger();
        r.require("quit closes the window",!window.isVisible());
    } else {
        window.close();
    }
    if(!reading) {
        const auto saved=settings.load();
        r.check("closing saves the maximized state and original window size",saved.maximized && saved.mainGeometry.size()==normalSize,{{"width",saved.mainGeometry.width()},{"height",saved.mainGeometry.height()}});
    }
    history.waitForSaved(); player.shutdown();
}
void crashCheckpoint(Report &r,const QString &fixtures,bool reading)
{
    const auto path=QDir(fixtures).absoluteFilePath("../zopana-ui-m2-v1/long-grid.mp4"); SettingsStore settings(r.output+"/persist/config"); HistoryStore history(r.output+"/persist/data");
    if(reading) { const auto entry=history.find(path); r.check("abrupt exit recovers last periodic checkpoint",entry && entry->resumable() && entry->position>=45 && entry->position<60); return; }
    auto p=settings.load(); p.recordHistory=true; r.require("enable history for checkpoint test",settings.save(p));
    history.clear(); history.waitForSaved(); Player player; r.require("checkpoint engine initialized",player.initialize()); MainWindow window(player); SessionController session(player,window,settings,history); player.setVolume(0); window.show(); auto *video=window.findChild<VideoWidget *>();
    r.require("checkpoint renderer ready",until([&] { return video->rendererReady(); })); player.openFile(path); r.require("checkpoint media loaded",until([&] { return player.state()==Player::State::Loaded && player.lastPosition()>0; }));
    player.seek(45); r.require("checkpoint seek reached",until([&] { return player.position()>=45 && player.position()<47; }));
    r.require("five second timer records progress",until([&] { const auto entry=history.find(path); return entry && entry->position>=45; },7000));
    r.require("periodic checkpoint reaches disk",until([&] { HistoryStore reader(r.output+"/persist/data"); const auto entry=reader.find(path); return entry && entry->position>=45; }));
    write(r.output+"/checkpoint-ready","ready");
    // The runner terminates this owned child after its atomic checkpoint exists.
    for(int i=0;i<6000;++i) QTest::qWait(10);
    player.shutdown(); throw std::runtime_error("runner did not terminate checkpoint child");
}
}
int main(int argc,char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QApplication app(argc,argv); SettingsStore::setApplicationIdentity(); app.setQuitOnLastWindowClosed(false);
    if(app.arguments().size()!=4) return 2; const auto mode=app.arguments()[1],fixtures=app.arguments()[2],output=app.arguments()[3];
    if(mode=="ui" || mode=="bindings") { try { Suite suite(fixtures,output); try { if(mode=="ui") suite.run(); else { suite.dialog(); suite.open(suite.fixtures+QString::fromUtf8("/网格 multi.mkv")); suite.bindings(); } } catch(const std::exception &e) { suite.check("aborted",false,{{"reason",e.what()}}); } return suite.save()?0:1; } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 2; } }
    Report report(output); try { if(mode=="stores") stores(report); else if(mode=="write" || mode=="write-fullscreen" || mode=="read") roundtrip(report,QDir::fromNativeSeparators(QFileInfo(fixtures).absoluteFilePath()),mode=="read",mode=="write-fullscreen"); else if(mode=="crash-write" || mode=="crash-read") crashCheckpoint(report,QDir::fromNativeSeparators(QFileInfo(fixtures).absoluteFilePath()),mode=="crash-read"); else return 2; } catch(const std::exception &e) { report.check("aborted",false,{{"reason",e.what()}}); } return report.save()?0:1;
}
