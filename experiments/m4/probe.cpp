#include "mainwindow.h"
#include "playeractions.h"
#include "videowidget.h"
#include "mediacontrols.h"
#include "captureservice.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "videosurface.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
bool until(const std::function<bool()> &condition, int timeout = 8000)
{
    QElapsedTimer timer; timer.start(); while (!condition() && timer.elapsed() < timeout) QTest::qWait(10); return condition();
}
class Suite
{
public:
    Player player;
    MainWindow window;
    VideoWidget *video;
    CaptureService *captures;
    mpv_handle *observer;
    QString fixtures, output;
    Player::State state = Player::State::Idle;
    QJsonArray checks;
    Suite(QString media,QString destination) : window(initialize()), fixtures(QDir::fromNativeSeparators(media)), output(QDir::fromNativeSeparators(destination))
    {
        QDir().mkpath(output); video = window.findChild<VideoWidget *>(); captures = window.findChild<CaptureService *>(); captures->setDirectory(output + "/captures");
        observer = mpv_create_client(player.handle(),"m4-test-observer"); player.setVolume(0);
        QObject::connect(&player,&Player::mediaChanged,&window,[this](auto value,const QString &,const QString &) { state = value; });
        window.show(); window.raise(); window.activateWindow(); require("renderer ready",until([this] { return video->rendererReady(); }));
        require("window active",QTest::qWaitForWindowActive(&window)); video->setFocus();
    }
    Player &initialize() { if (!player.initialize()) throw std::runtime_error("initialize failed"); return player; }
    ~Suite() { player.shutdown(); mpv_destroy(observer); }
    QAction *action(const char *id) { return window.findChild<QAction *>(id); }
    void check(QString name,bool passed,QJsonObject fields = {})
    {
        fields["name"] = name; fields["passed"] = passed; checks.append(fields); std::printf("%s %s\n",passed?"PASS":"FAIL",name.toUtf8().constData()); std::fflush(stdout);
    }
    void require(QString name,bool passed) { check(name,passed); if(!passed) throw std::runtime_error(name.toStdString()); }
    double number(const char *name) { double value=-100; mpv_get_property(observer,name,MPV_FORMAT_DOUBLE,&value); return value; }
    bool flag(const char *name) { int value=0; mpv_get_property(observer,name,MPV_FORMAT_FLAG,&value); return value; }
    QVariant property(const char *name)
    {
        mpv_node value{}; if (mpv_get_property(observer,name,MPV_FORMAT_NODE,&value)<0) return {};
        const auto result=nodeValue(value); mpv_free_node_contents(&value); return result;
    }
    void command(const QStringList &arguments)
    {
        std::vector<QByteArray> bytes; for(const auto &s:arguments) bytes.push_back(s.toUtf8());
        std::vector<const char *> args; for(const auto &s:bytes) args.push_back(s.constData()); args.push_back(nullptr);
        if(mpv_command(observer,args.data())<0) throw std::runtime_error("observer command failed");
    }
    void open(const QString &name=QString::fromUtf8("网格 multi.mkv"))
    {
        player.openFile(fixtures+'/'+name); require("media loaded",until([this,&name] { return state==Player::State::Loaded && (name=="audio.m4a" || video->canCapture()); }));
        player.setPaused(true); require("pause confirmed",until([this] { return flag("pause"); })); QTest::qWait(100); video->setFocus();
    }
    void seek(double target) { player.seek(target); require(QString("seek %1").arg(target),until([&] { return std::abs(number("time-pos")-target)<.05; })); QTest::qWait(80); }
    void key(Qt::Key key,Qt::KeyboardModifiers modifiers=Qt::NoModifier) { QTest::keyClick(video,key,modifiers); }
    void shot(const QString &name) { check("screenshot "+name,window.grab().save(output+'/'+name+".png")); }
    void tracks()
    {
        check("advanced actions disabled without media",!action("capture.source")->isEnabled() && !action("sub.load")->isEnabled() && !action("frame.forward")->isEnabled());
        check("new sessions start at normal speed",std::abs(player.mediaProperty("speed").toDouble()-1)<.001);
        open(); require("tracks reported",until([this] { return player.tracks().size()==5; }));
        auto *audio=window.findChild<QMenu *>("audioTracksMenu"); auto *subs=window.findChild<QMenu *>("subtitleTracksMenu");
        check("menus use actual audio and subtitle tracks",audio->actions().size()==2 && subs->actions().size()==2);
        auto *second=audio->actions()[1]; const auto secondId=second->property("trackId").toLongLong(); second->trigger();
        require("audio selection confirmed by engine",until([&] { return number("aid")==secondId; }));
        check("menu check follows audio feedback",until([&] { return audio->actions()[1]->isChecked() && !audio->actions()[0]->isChecked(); }));
        key(Qt::Key_J); require("J cycles audio",until([&] { return number("aid")!=secondId; }));
        const auto sid=number("sid"); key(Qt::Key_H); require("H hides subtitles",until([this] { return !flag("sub-visibility"); }));
        check("subtitle visibility keeps selected track",number("sid")==sid);
        key(Qt::Key_K); require("K cycles actual tracks while hidden",until([&] { return number("sid")!=sid; })); check("cycling does not change visibility",!flag("sub-visibility"));
        key(Qt::Key_H); until([this] { return flag("sub-visibility"); });
        for(auto extension:{"srt","ass","vtt"}) {
            QSignalSpy loaded(&player,&Player::subtitleLoaded); player.addSubtitle(fixtures+"/external."+extension);
            require(QString("load external %1").arg(extension),until([&] { return loaded.count()==1; }));
            check("selected external track is reported",until([this] { const auto sid=player.mediaProperty("sid").toLongLong(); for(const auto &track:player.tracks()) if(track.type=="sub" && track.external && track.id==sid) return true; return false; }));
        }
        const auto previous=number("sid"); QSignalSpy errors(&player,&Player::commandError); player.addSubtitle(output+"/missing.srt");
        require("bad subtitle reports failure",until([&] { return !errors.isEmpty(); })); check("bad subtitle keeps original selection",number("sid")==previous);
        bool chooser=false; QTimer::singleShot(100,&window,[&] { auto *dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); if(dialog) { chooser=true; dialog->reject(); } });
        key(Qt::Key_L,Qt::ControlModifier); check("subtitle chooser cancellation keeps selection",chooser && number("sid")==previous);
        const auto oldGeneration=player.generation(); open("grid.mp4"); player.selectTrack("audio",secondId,oldGeneration); QTest::qWait(50);
        check("previous file track ID is never reused",player.tracks().size()==1 && !action("audio.nextTrack")->isEnabled());
        open("audio.m4a"); check("audio-only disables frame and captures",!action("frame.forward")->isEnabled() && !action("capture.viewport")->isEnabled());
        QSignalSpy submitted(&player,&Player::requestSubmitted); player.nextTrack("audio"); check("single audio track is not resubmitted",submitted.isEmpty());
        open();
    }
    void adjustments()
    {
        key(Qt::Key_BracketRight); require("speed key increases quarter step",until([this] { return std::abs(number("speed")-1.25)<.001; }));
        key(Qt::Key_BracketLeft); require("speed key decreases quarter step",until([this] { return std::abs(number("speed")-1)<.001; }));
        action("rate.preset.150")->trigger(); require("speed preset uses real property",until([this] { return number("speed")==1.5; }));
        check("preset check follows feedback",until([this] { return action("rate.preset.150")->isChecked(); }));
        for(int i=0;i<20;++i) player.adjustMediaProperty("speed",.25);
        require("speed upper limit is three",until([this] { return number("speed")==3; }));
        for(int i=0;i<20;++i) player.adjustMediaProperty("speed",-.25);
        require("speed lower limit is quarter",until([this] { return number("speed")==.25; })); key(Qt::Key_Backspace); until([this] { return number("speed")==1; });
        action("audio.pitchCorrection")->trigger(); require("pitch correction toggles",until([this] { return !flag("audio-pitch-correction"); })); action("audio.pitchCorrection")->trigger(); until([this] { return flag("audio-pitch-correction"); });
        key(Qt::Key_Period); require("subtitle delay step",until([this] { return std::abs(number("sub-delay")-.1)<.001; }));
        key(Qt::Key_Greater,Qt::ShiftModifier); require("shift punctuation affects only audio delay",until([this] { return std::abs(number("audio-delay")-.1)<.001; }));
        check("audio punctuation does not also alter subtitle delay",std::abs(number("sub-delay")-.1)<.001);
        key(Qt::Key_Less,Qt::ShiftModifier); require("shift comma lowers audio delay",until([this] { return std::abs(number("audio-delay"))<.001; }));
        key(Qt::Key_Equal,Qt::ControlModifier); require("Ctrl equals changes subtitle scale",until([this] { return std::abs(number("sub-scale")-1.1)<.001; }));
        check("Ctrl equals does not zoom video",video->zoomScale()==1);
        for(int i=0;i<20;++i) player.adjustMediaProperty("sub-scale",.1);
        require("subtitle scale clamps at two",until([this] { return number("sub-scale")==2; }));
        for(int i=0;i<30;++i) player.adjustMediaProperty("sub-scale",-.1);
        require("subtitle scale clamps at half",until([this] { return number("sub-scale")==.5; })); key(Qt::Key_0,Qt::ControlModifier); until([this] { return number("sub-scale")==1; });
        check("numeric controls show engine feedback",until([this] { return std::abs(window.findChild<QDoubleSpinBox *>("subDelaySpin")->value()-.1)<.001; }));
        action("audio.delayReset")->trigger(); action("sub.delayReset")->trigger(); until([this] { return number("sub-delay")==0 && number("audio-delay")==0; });
        player.setMediaProperty("speed",2.0); until([this] { return number("speed")==2; }); player.setMediaProperty("audio-delay",.3); player.setMediaProperty("sub-delay",.4);
        until([this] { return std::abs(number("sub-delay")-.4)<.001; }); open("grid.mp4");
        check("file change keeps session speed but clears delays",number("speed")==2 && number("sub-delay")==0 && number("audio-delay")==0);
        player.setMediaProperty("speed",1.0); until([this] { return number("speed")==1; }); open(); seek(1.2);
        key(Qt::Key_D); require("frame command advances exactly one 25fps frame",until([this] { return std::abs(number("time-pos")-1.24)<.01 && flag("pause"); }));
        player.setPaused(false); until([this] { return !flag("pause"); }); player.frameStep(); require("frame step from playback ends paused",until([this] { return flag("pause"); }));
    }
    void matchingAndRequests()
    {
        const auto bank=QDir(fixtures).absoluteFilePath("../zopana-ui-m4-v1");
        player.openFile(bank+"/match.mp4");
        require("same-name and language subtitles load",until([this] { return state==Player::State::Loaded && player.tracks().size()==4; }));
        QStringList external; for(const auto &track:player.tracks()) if(track.external) external.append(QFileInfo(track.externalPath).fileName());
        external.sort(); check("unrelated and non-language subtitles excluded",external==QStringList({"match.en.ass","match.srt","match.zh-Hans.vtt"}));
        player.addSubtitle(bank+"/external.ssa"); require("manual SSA subtitle loads",until([this] { return player.tracks().size()==5; }));
        open();
        const auto oldAid=number("aid"); QSignalSpy errors(&player,&Player::commandError);
        player.setMediaProperty("aid","not-a-track"); require("invalid track request reports failure",until([&] { return !errors.isEmpty(); }));
        check("failed property rolls back target and menu",until([&] { auto *menu=window.findChild<QMenu *>("audioTracksMenu"); for(auto *a:menu->actions()) if(a->property("trackId").toLongLong()==oldAid) return a->isChecked() && player.propertyTarget("aid").toLongLong()==oldAid; return false; }));
        QSignalSpy submitted(&player,&Player::requestSubmitted), finished(&player,&Player::requestFinished), subtitles(&player,&Player::subtitleLoaded), images(&player,&Player::sourceImageReady);
        player.selectTrack("audio",2,player.generation()); player.addSubtitle(fixtures+"/external.ass"); player.captureSource();
        player.openFile(fixtures+"/grid.mp4"); player.openFile(fixtures+QString::fromUtf8("/换片 C.mkv"));
        require("rapid media changes settle on final file",until([this] { return state==Player::State::Loaded && player.path().endsWith(QString::fromUtf8("换片 C.mkv")) && player.tracks().size()==5; })); QTest::qWait(100);
        QSet<quint64> ids; bool unique=true, scoped=true, stale=false;
        for(const auto &row:submitted) { unique&=!ids.contains(row[0].toULongLong()); ids.insert(row[0].toULongLong()); scoped&=row[1].toULongLong()>0 && row[2].toULongLong()>0; }
        for(const auto &row:finished) stale|=!row[1].toBool();
        check("requests have unique IDs and generation epoch scope",unique && scoped && ids.size()>=6);
        check("old property subtitle and capture replies are discarded",stale && subtitles.isEmpty() && images.isEmpty() && player.tracks().size()==5);
        open();
    }
    void loops()
    {
        player.clearLoop(); until([this] { return property("ab-loop-a").toString()=="no"; }); seek(2);
        QSignalSpy notices(&player,&Player::notice); key(Qt::Key_B);
        require("B without A establishes A only",until([this] { return std::abs(number("ab-loop-a")-2)<.05; })); check("missing B prompt is visible",!notices.isEmpty() && property("ab-loop-b").toString()=="no");
        seek(2.2); key(Qt::Key_B); QTest::qWait(80); check("AB interval below half second rejected",property("ab-loop-b").toString()=="no");
        seek(3); key(Qt::Key_B); require("valid B starts engine loop",until([this] { return std::abs(number("ab-loop-b")-3)<.05; }));
        seek(2.5); check("seek inside AB retains interval",player.loopActive());
        command({"set","speed","3"}); player.setPaused(false); int wraps=0; double previous=number("time-pos");
        for(int i=0;i<45;++i) { QTest::qWait(35); const auto current=number("time-pos"); if(current<previous-.2) ++wraps; previous=current; }
        check("AB wraps without queue EOF",wraps>=2 && state==Player::State::Loaded,{{"wraps",wraps}});
        player.setPaused(true); until([this] { return flag("pause"); }); seek(5);
        check("seek outside AB clears markers",until([this] { return property("ab-loop-a").toString()=="no" && property("ab-loop-b").toString()=="no"; }));
        command({"set","speed","1"}); seek(2); key(Qt::Key_A); seek(3); key(Qt::Key_B); QTest::qWait(80); shot("ab-markers");
        key(Qt::Key_B,Qt::ControlModifier); check("Ctrl B clears loop",until([this] { return !player.loopActive(); }));
    }
    void screenshots()
    {
        seek(1.2); player.setMediaProperty("sub-visibility",true); player.setMediaProperty("sub-scale",1.0); QTest::qWait(100);
        QSignalSpy saved(captures,&CaptureService::saved), failed(captures,&CaptureService::failed);
        QString firstSource; QImage previousViewport;
        for(int level:{0,5,40}) {
            video->resetView(); for(int i=0;i<level;++i) video->zoomSteps(1); QTest::qWait(150);
            const auto frame=video->grabFramebuffer(); const int before=saved.count(); captures->source();
            require("source capture saved asynchronously",until([&] { return saved.count()>before; })); const auto source=saved.last()[0].toString();
            QImage image(source); check("source keeps original resolution",image.size()==QSize(960,540));
            if(firstSource.isEmpty()) firstSource=source; else check("source is invariant under zoom",image.convertToFormat(QImage::Format_RGB888)==QImage(firstSource).convertToFormat(QImage::Format_RGB888));
            const int count=saved.count(); captures->viewport(); require("viewport capture saved asynchronously",until([&] { return saved.count()>count; }));
            QImage viewport(saved.last()[0].toString()); check("viewport contains exact composed FBO without OSD",viewport.convertToFormat(QImage::Format_RGB888)==frame.convertToFormat(QImage::Format_RGB888));
            check("viewport uses physical DPI dimensions",viewport.size()==QSize(qRound(video->width()*video->devicePixelRatioF()),qRound(video->height()*video->devicePixelRatioF())));
            if(!previousViewport.isNull()) check("zoom changes viewport pixels",viewport!=previousViewport); previousViewport=viewport;
            check("encoding and writes occur on worker thread",saved.last()[1].toBool());
        }
        const int count=saved.count(); captures->viewport(); captures->viewport(); require("two captures both complete",until([&] { return saved.count()>=count+2; }));
        check("filenames do not collide",saved[saved.count()-1][0]!=saved[saved.count()-2][0]);
        check("capture success offers folder button",window.findChild<QToolButton *>("captureFolderButton")->isVisible() && action("capture.openFolder")->isEnabled());
        const auto lastSaved=captures->lastSaved(); QFile blocker(output+"/not-a-directory"); require("create failure destination",blocker.open(QIODevice::WriteOnly)); blocker.write("sentinel"); blocker.close(); captures->setDirectory(blocker.fileName());
        captures->viewport(); require("unwritable destination reports failure",until([&] { return !failed.isEmpty(); })); check("failure does not report false success",captures->lastSaved()==lastSaved && saved.count()==count+2);
        captures->setDirectory(output+"/captures"); video->resetView();
        const int stale=saved.count(); captures->source(); player.openFile(fixtures+"/grid.mp4"); player.openFile(fixtures+"/portrait.mp4");
        require("latest file wins during source capture",until([this] { return state==Player::State::Loaded && player.path().endsWith("portrait.mp4"); })); QTest::qWait(150);
        check("old screenshot never reports success on replacement",saved.count()==stale);
        open();
    }
    void windows()
    {
        auto *controls=window.findChild<MediaControls *>(); const auto info=controls->information();
        check("information reports actual codec and media parameters",info.value(QStringLiteral("视频宽度")).toInt()==960 && info.value(QStringLiteral("容器帧率")).toDouble()==25 && info.value(QStringLiteral("视频编码/解码器")).toString().contains("264"));
        bool dialogSeen=false; QTimer::singleShot(100,&window,[&] { auto *dialog=QApplication::activeModalWidget(); if(dialog) { auto *table=dialog->findChild<QTableWidget *>("mediaInfoTable"); dialogSeen=table && table->rowCount()>=15; QTest::keyClick(dialog,Qt::Key_Escape); } });
        key(Qt::Key_I); check("I opens media information",dialogSeen);
        video->zoomSteps(5); QTest::qWait(150); const auto frame=video->grabFramebuffer(); const auto generation=player.generation();
        auto *host=window.findChild<PlaylistHost *>(); host->setDetached(true); host->setVisible(true,false);
        window.raise(); window.activateWindow(); require("main active for topmost",QTest::qWaitForWindowActive(&window)); video->setFocus(); key(Qt::Key_T); QTest::qWait(120);
        check("T sets main topmost flag and check",window.windowFlags().testFlag(Qt::WindowStaysOnTopHint) && action("window.alwaysOnTop")->isChecked());
        check("main topmost does not propagate to independent list",!host->window()->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        require("pause and renderer remain usable after topmost",until([this] { return state==Player::State::Loaded && video->canCapture() && flag("pause"); }));
        check("topmost preserves media generation and view",player.generation()==generation && video->grabFramebuffer()==frame);
        window.raise(); window.activateWindow(); require("main reactivated after pin",QTest::qWaitForWindowActive(&window)); video->setFocus(); key(Qt::Key_F11); QTest::qWait(100);
        require("F11 enters fullscreen before pin toggle",window.isFullScreen()); window.raise(); window.activateWindow(); require("fullscreen main active",QTest::qWaitForWindowActive(&window)); video->setFocus(); key(Qt::Key_T); QTest::qWait(100);
        check("topmost toggle preserves fullscreen state",window.isFullScreen() && !window.windowFlags().testFlag(Qt::WindowStaysOnTopHint),{{"fullscreen",window.isFullScreen()},{"pinned",window.windowFlags().testFlag(Qt::WindowStaysOnTopHint)}});
        if(window.isFullScreen()) action("window.fullscreen")->trigger(); QTest::qWait(100);
        host->setVisible(false,false); host->setDetached(false); video->resetView();
        auto *edit=new QLineEdit(&window); edit->setGeometry(20,60,260,32); edit->show(); edit->raise(); edit->setFocus();
        QSignalSpy commands(&player,&Player::requestSubmitted); QTest::keyClicks(edit,"hjkdabi[],. ");
        check("editors protect all new watching shortcuts",commands.isEmpty() && !window.isFullScreen()); delete edit; video->setFocus();
        open("audio.m4a"); bool unknown=false; const auto audioInfo=controls->information(); unknown=!audioInfo.value(QStringLiteral("视频宽度")).isValid(); check("audio information leaves video fields unknown",unknown);
        open(); shot("m4-player");
    }
    void run() { tracks(); adjustments(); matchingAndRequests(); loops(); screenshots(); windows(); }
    bool save()
    {
        bool passed=true; for(const auto &c:checks) passed&=c.toObject()["passed"].toBool();
        QFile file(output+"/results.json"); if(!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(QJsonObject{{"passed",passed},{"devicePixelRatio",video->devicePixelRatioF()},{"checks",checks}}).toJson()); return passed;
    }
};
class RecoverySuite
{
public:
    QString fixtures, output;
    QJsonArray checks;
    double dpr=1;
    RecoverySuite(QString media,QString destination) : fixtures(QDir::fromNativeSeparators(media)), output(QDir::fromNativeSeparators(destination)) { QDir().mkpath(output); }
    void check(QString name,bool passed,QJsonObject fields = {})
    {
        fields["name"]=name; fields["passed"]=passed; checks.append(fields); std::printf("%s %s\n",passed?"PASS":"FAIL",name.toUtf8().constData()); std::fflush(stdout);
    }
    void require(QString name,bool passed) { check(name,passed); if(!passed) throw std::runtime_error(name.toStdString()); }
    void run()
    {
        Player engine; require("recovery engine initializes",engine.initialize()); engine.setVolume(0);
        QWidget host; host.resize(960,640); dpr=host.devicePixelRatioF(); VideoWidget *view=nullptr; bool seekable=false; double position=0;
        QObject::connect(&engine,&Player::seekableChanged,&host,[&](bool value) { seekable=value; });
        QObject::connect(&engine,&Player::positionChanged,&host,[&](double value) { position=value; });
        auto make=[&] { view=new VideoWidget(engine.handle(),&host); view->setGeometry(host.rect()); QObject::connect(view,&VideoWidget::rendererReadyChanged,&engine,&Player::setRendererReady); view->show(); };
        const auto cleanup=qScopeGuard([&] { engine.shutdown(); delete view; view=nullptr; host.close(); });
        QObject::connect(&engine,&Player::videoSizeChanged,&host,[&](QSize size) { if(view) view->setVideoSize(size); }); make(); host.show(); host.raise(); host.activateWindow();
        require("standalone recovery host active",QTest::qWaitForWindowActive(&host));
        require("recovery renderer ready",until([&] { return view->rendererReady(); })); engine.openFile(fixtures+QString::fromUtf8("/网格 multi.mkv"),42);
        require("recovery file loaded",until([&] { return engine.state()==Player::State::Loaded && engine.tracks().size()==5; }));
        require("recovery seek capability ready",until([&] { return seekable && engine.lastPosition()>0; }));
        engine.setPaused(true); engine.seek(2.4); require("recovery position ready",until([&] { return std::abs(position-2.4)<.05; })); QTest::qWait(150);
        const auto ass = fixtures + "/external.ass";
        const auto srt = fixtures + "/external.srt";
        const auto externalId = [&](const QString &path) -> qlonglong {
            for (const auto &track : engine.tracks()) {
                if (track.external && mediaPathKey(track.externalPath) == mediaPathKey(path)) return track.id;
            }
            return 0;
        };
        QSignalSpy subtitles(&engine, &Player::subtitleLoaded);
        engine.addSubtitle(ass);
        require("ASS finishes loading before SRT", until([&] {
            return subtitles.count() == 1 && externalId(ass) > 0;
        }));
        const auto assId = externalId(ass);
        engine.addSubtitle(srt);
        require("recovery externals visible with SRT after ASS", until([&] {
            return subtitles.count() == 2 && engine.tracks().size() == 7 && externalId(srt) > assId;
        }));
        const auto removeId=QByteArray::number(assId); const char *remove[]={"sub-remove",removeId.constData(),nullptr};
        require("remove external to create track ID gap", mpv_command(engine.handle(), remove) >= 0 && until([&] {
            return engine.tracks().size() == 6 && externalId(ass) == 0;
        }));
        engine.addSubtitle(ass);
        require("readded external has new track ID", until([&] {
            const auto newId = externalId(ass);
            return subtitles.count() == 3 && engine.tracks().size() == 7 && newId > assId && engine.mediaProperty("sid").toLongLong() == newId;
        }));
        engine.selectTrack("audio",2,engine.generation()); engine.setMediaProperty("speed",1.5); engine.setMediaProperty("sub-scale",1.2);
        engine.setMediaProperty("sub-delay",.2); engine.setMediaProperty("audio-delay",.1); engine.setMediaProperty("sub-visibility",false);
        engine.setMediaProperty("ab-loop-a",2.0); engine.setMediaProperty("ab-loop-b",4.0); QTest::qWait(200);
        const auto selected=engine.mediaProperty("sid").toLongLong(); QString fingerprint;
        for(const auto &track:engine.tracks()) if(track.type=="sub" && track.id==selected) fingerprint=track.fingerprint();
        const auto generation=engine.generation(), epoch=engine.renderEpoch(); QSignalSpy restored(&engine,&Player::restored);
        engine.setRendererReady(false); engine.setMediaProperty("speed",1.75); delete view; view=nullptr; make();
        require("full renderer destruction and recreation restores",until([&] { return !restored.isEmpty() && engine.state()==Player::State::Loaded; })); QTest::qWait(100);
        check("recovery keeps generation and item but advances render epoch",engine.generation()==generation && engine.itemId()==42 && engine.renderEpoch()>epoch);
        check("recovery restores details and honors newest global intent",engine.mediaProperty("aid").toLongLong()==2 && engine.mediaProperty("speed").toDouble()==1.75 && std::abs(engine.mediaProperty("sub-delay").toDouble()-.2)<.001 && std::abs(engine.mediaProperty("audio-delay").toDouble()-.1)<.001 && std::abs(engine.mediaProperty("sub-scale").toDouble()-1.2)<.001 && !engine.mediaProperty("sub-visibility").toBool());
        bool selectedExternal=false; for(const auto &track:engine.tracks()) selectedExternal|=track.fingerprint()==fingerprint && track.id==engine.mediaProperty("sid").toLongLong();
        check("recovery matches fingerprint after numeric track IDs change",selectedExternal && engine.mediaProperty("sid").toLongLong()!=selected && engine.tracks().size()==7 && engine.loopActive());
        check("recovery retains paused position inside AB",std::abs(position-2.4)<.05);
        const auto missing=output+"/recovery-missing.srt"; QFile::remove(missing); require("copy temporary subtitle",QFile::copy(fixtures+"/external.srt",missing));
        engine.addSubtitle(missing); require("temporary subtitle selected",until([&] { for(const auto &track:engine.tracks()) if(mediaPathKey(track.externalPath)==mediaPathKey(missing) && track.id==engine.mediaProperty("sid").toLongLong()) return engine.tracks().size()==8; return false; }));
        require("remove temporary subtitle before recovery",QFile::remove(missing)); QSignalSpy notices(&engine,&Player::notice);
        engine.setRendererReady(false); delete view; view=nullptr; make();
        require("recovery completes with missing selected subtitle",until([&] { return restored.count()==2 && engine.state()==Player::State::Loaded; }));
        check("missing subtitle warns and falls back to disabled",!notices.isEmpty() && engine.mediaProperty("sid").toString()=="no" && engine.tracks().size()==7,{{"sid",QJsonValue::fromVariant(engine.mediaProperty("sid"))},{"tracks",engine.tracks().size()},{"notices",notices.count()}});
        engine.shutdown(); delete view; view=nullptr; host.close();
    }
    bool save()
    {
        bool passed=true; for(const auto &c:checks) passed&=c.toObject()["passed"].toBool();
        QFile file(output+"/results.json"); if(!file.open(QIODevice::WriteOnly)) return false;
        file.write(QJsonDocument(QJsonObject{{"passed",passed},{"devicePixelRatio",dpr},{"checks",checks}}).toJson()); return passed;
    }
};
}
int main(int argc,char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QApplication app(argc,argv); app.setApplicationName("zopana"); app.setQuitOnLastWindowClosed(false);
    if(app.arguments().size()==4 && app.arguments()[1]=="--engine-recovery") {
        RecoverySuite suite(app.arguments()[2],app.arguments()[3]); try { suite.run(); } catch(const std::exception &error) { suite.check("aborted",false,{{"reason",error.what()}}); } return suite.save()?0:1;
    }
    if(app.arguments().size()==4 && app.arguments()[1]=="--recovery") {
        Suite suite(app.arguments()[2],app.arguments()[3]); try { suite.open(); suite.windows(); } catch(const std::exception &error) { suite.check("aborted",false,{{"reason",error.what()}}); } return suite.save()?0:1;
    }
    if(app.arguments().size()!=3) return 2;
    try { Suite suite(app.arguments()[1],app.arguments()[2]); try{suite.run();}catch(const std::exception &error){suite.check("aborted",false,{{"reason",error.what()}});}return suite.save()?0:1;}
    catch(const std::exception &error){std::fprintf(stderr,"%s\n",error.what());return 2;}
}
