#include "sessioncontroller.h"
#include "settingsdialog.h"
#include "player.h"
#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "videowidget.h"
#include "videosurface.h"
#include "captureservice.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <cmath>

SessionController::SessionController(Player &player,MainWindow &window,SettingsStore &settings,HistoryStore &history)
    : QObject(&window),player_(player),window_(window),settings_(settings),history_(history),preferences_(settings.load())
{
    setObjectName("sessionController"); actions_=window.findChild<PlayerActions *>(); playlist_=window.findChild<PlaylistController *>(); host_=window.findChild<PlaylistHost *>(); surface_=window.findChild<VideoSurface *>(); video_=window.findChild<VideoWidget *>();
    recent_=new QMenu(QStringLiteral("最近文件"),&window); recent_->setObjectName("recentFilesMenu"); auto *file=window.findChild<QMenu *>("fileMenu"); file->insertMenu(actions_->get(PlayerActions::Quit),recent_); file->insertAction(actions_->get(PlayerActions::Quit),actions_->get(PlayerActions::ClearHistory));
    window.findChild<QMenu *>("viewMenu")->addAction(actions_->get(PlayerActions::Settings));
    window.findChild<QMenu *>("videoContextMenu")->addAction(actions_->get(PlayerActions::Settings));
    actions_->get(PlayerActions::Settings)->setEnabled(true); actions_->get(PlayerActions::ClearHistory)->setEnabled(true);
    connect(actions_->get(PlayerActions::Settings),&QAction::triggered,this,&SessionController::showSettings); connect(actions_->get(PlayerActions::ClearHistory),&QAction::triggered,this,&SessionController::clearHistory);
    prompt_=new QWidget(surface_); prompt_->setObjectName("resumePrompt"); prompt_->setAutoFillBackground(true); auto *layout=new QVBoxLayout(prompt_);
    promptText_=new QLabel; promptText_->setWordWrap(true); promptText_->setTextFormat(Qt::PlainText); layout->addWidget(promptText_);
    auto *row=new QHBoxLayout; auto *resume=new QPushButton(QStringLiteral("继续")),*start=new QPushButton(QStringLiteral("从头播放")); resume->setObjectName("resumeContinueButton"); resume->setDefault(true); start->setObjectName("resumeStartButton"); row->addWidget(resume); row->addWidget(start); layout->addLayout(row); prompt_->hide();
    connect(surface_,&VideoSurface::overlayTopChanged,this,&SessionController::arrangePrompt);
    connect(resume,&QPushButton::clicked,this,[this] { chooseResume(true); }); connect(start,&QPushButton::clicked,this,[this] { chooseResume(false); });
    resumeTimer_.setSingleShot(true); resumeTimer_.setInterval(8000); connect(&resumeTimer_,&QTimer::timeout,this,[this] { chooseResume(false); });
    recentTimer_.setSingleShot(true); connect(&recentTimer_,&QTimer::timeout,this,&SessionController::rebuildRecent);
    connect(&history_,&HistoryStore::changed,this,[this] { recentTimer_.start(0); }); connect(recent_,&QMenu::aboutToShow,this,&SessionController::rebuildRecent);
    connect(&history_,&HistoryStore::error,this,[this](const QString &message) { surface_->showOsd("storage",message,5000); });
    progressTimer_.setInterval(5000); connect(&progressTimer_,&QTimer::timeout,this,[this] { flushProgress(); }); progressTimer_.start();
    windowTimer_.setSingleShot(true); windowTimer_.setInterval(700); connect(&windowTimer_,&QTimer::timeout,this,&SessionController::saveWindows);
    window.installEventFilter(this); host_->window()->installEventFilter(this); surface_->installEventFilter(this);
    connect(host_,&PlaylistHost::changed,this,[this] { if(!restoring_) windowTimer_.start(); });
    connect(&player_,&Player::volumeChanged,this,[this] { if(!restoring_) windowTimer_.start(); }); connect(&player_,&Player::mutedChanged,this,[this] { if(!restoring_) windowTimer_.start(); });
    connect(&player_,&Player::detailsChanged,this,[this] { tryResume(); if(!restoring_) windowTimer_.start(); });
    connect(actions_,&PlayerActions::invoked,this,[this] { if(!restoring_) windowTimer_.start(); });
    connect(&player_,&Player::fileOpening,this,[this] { flushProgress(); dismissResume(); recordCurrent_=preferences_.recordHistory; });
    connect(&player_,&Player::stopping,this,[this] { flushProgress(); dismissResume(); recordCurrent_=false; });
    connect(&player_,&Player::mediaChanged,this,[this](Player::State state,const QString &path,const QString &) {
        if(state==Player::State::Loading && !player_.lastLoadWasRecovery()) { currentPath_=path; prepareFile(); }
        if(state==Player::State::Loaded && !player_.lastLoadWasRecovery()) { tryResume(); if(!awaitingResume_) flushProgress(); }
        if(state==Player::State::Ended) { dismissResume(); flushProgress(true); recordCurrent_=false; }
        if(state==Player::State::Error) { dismissResume(); recordCurrent_=false; }
        if(state==Player::State::Idle && path.isEmpty()) { currentPath_.clear(); recentTimer_.start(0); }
    });
    connect(&player_,&Player::durationChanged,this,[this] { tryResume(); }); connect(&player_,&Player::seekableChanged,this,[this] { tryResume(); });
    connect(&player_,&Player::audioAvailableChanged,this,[this] { tryResume(); });
    connect(video_,&QOpenGLWidget::frameSwapped,this,[this] { tryResume(); });
    connect(&player_,&Player::pausedChanged,this,[this](bool paused) { if(!paused && awaitingResume_) dismissResume(); if(paused) flushProgress(); });
    connect(&player_,&Player::positionChanged,this,[this](double position) { if(resumeSeek_ && std::abs(position-*resumeSeek_)<.15) { resumeSeek_.reset(); player_.setPaused(false); } });
    connect(&player_,&Player::seekRequested,this,[this](double) { if(choosing_) return; if(awaitingResume_ || resumeSeek_) { dismissResume(); player_.setPaused(false); } });
    connect(qApp,&QGuiApplication::screenRemoved,this,[this] { QTimer::singleShot(0,this,&SessionController::ensureVisibleWindows); });
    applyRuntime();
    if(preferences_.mainGeometry.isValid()) window_.setGeometry(preferences_.mainGeometry);
    if(preferences_.playlistGeometry.isValid()) host_->window()->setGeometry(preferences_.playlistGeometry);
    ensureVisibleWindows(); if(preferences_.maximized) window_.setWindowState(Qt::WindowMaximized);
    host_->restore(preferences_.detached,preferences_.playlistVisible,preferences_.dockWidth); rebuildRecent(); restoring_=false;
    const auto message=settings_.error().isEmpty()?history_.loadError():settings_.error(); if(!message.isEmpty()) QTimer::singleShot(0,this,[this,message] { surface_->showOsd("storage",message,5000); });
}
void SessionController::captureWindows(Preferences &p) const
{
    if(!window_.isMinimized()) { p.maximized=window_.restoreMaximized(); p.mainGeometry=window_.isMaximized() || window_.isFullScreen()?window_.normalGeometry():window_.geometry(); }
    auto *list=host_->window(); const auto normal=list->normalGeometry(); p.playlistGeometry=list->isMinimized() || list->isMaximized()?normal:list->geometry();
    p.detached=host_->detached(); p.playlistVisible=host_->preferredVisible(); p.dockWidth=host_->dockWidth();
    p.volume=player_.volumeTarget(); p.muted=player_.muted(); p.standardMode=actions_->get(PlayerActions::StandardMode)->isChecked();
    p.subtitleScale=player_.propertyTarget("sub-scale").toDouble();
}
void SessionController::applyRuntime()
{
    window_.findChild<CaptureService *>()->setDirectory(preferences_.captureDirectory); player_.setTrackLanguages(preferences_.audioLanguages,preferences_.subtitleLanguages);
    player_.setMediaProperty("sub-scale",preferences_.subtitleScale); player_.setVolume(preferences_.volume); player_.setMuted(preferences_.muted);
    video_->setInputMode(preferences_.standardMode?VideoWidget::InputMode::Standard:VideoWidget::InputMode::Feature);
    actions_->get(preferences_.standardMode?PlayerActions::StandardMode:PlayerActions::FeatureMode)->setChecked(true);
    const auto ignored=actions_->applyBindings(preferences_.shortcuts); if(!ignored.isEmpty()) QTimer::singleShot(0,this,[this] { surface_->showOsd("settings",QStringLiteral("部分快捷键无效或冲突，已保留有效绑定和默认值。"),5000); });
}
bool SessionController::applyPreferences(Preferences p)
{
    // Layout and live audio values are not edited by this dialog.
    const double scale=p.subtitleScale; captureWindows(p); p.subtitleScale=scale;
    if(!settings_.save(p)) { settingsError(settings_.error()); return false; }
    const bool wasRecording=preferences_.recordHistory; preferences_=std::move(p); settingsErrorReported_=false;
    if(wasRecording && !preferences_.recordHistory) { recordCurrent_=false; dismissResume(); }
    else if(!wasRecording && preferences_.recordHistory && !currentPath_.isEmpty() && !suppressedPaths_.contains(mediaPathKey(currentPath_))) recordCurrent_=true;
    applyRuntime(); return true;
}
void SessionController::settingsError(const QString &message) { if(!settingsErrorReported_) { settingsErrorReported_=true; surface_->showOsd("storage",message,5000); } }
void SessionController::saveWindows()
{
    if(restoring_) return; captureWindows(preferences_); if(!settings_.save(preferences_)) settingsError(settings_.error()); else settingsErrorReported_=false;
}
void SessionController::ensureVisibleWindows()
{
    QList<QRect> screens; for(auto *screen:QGuiApplication::screens()) screens.append(screen->availableGeometry());
    const auto clamp=[&](QWidget *window,QSize minimum) { const auto requested=window->isMaximized() || window->isFullScreen() || window->isMinimized()?window->normalGeometry():window->geometry(); const auto rect=SettingsStore::visibleGeometry(requested,screens,minimum); if(!window->isFullScreen() && !window->isMaximized()) window->setGeometry(rect); return rect; };
    preferences_.mainGeometry=clamp(&window_,window_.minimumSize()); preferences_.playlistGeometry=clamp(host_->window(),host_->window()->minimumSize());
}
bool SessionController::eventFilter(QObject *object,QEvent *event)
{
    if(object==surface_ && event->type()==QEvent::Resize) arrangePrompt();
    if(object==&window_ && event->type()==QEvent::Close) { flushProgress(); saveWindows(); history_.waitForSaved(); }
    if(!restoring_ && (object==&window_ || object==host_->window()) && (event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::WindowStateChange)) windowTimer_.start();
    return QObject::eventFilter(object,event);
}
void SessionController::showSettings()
{
    if(settingsDialog_) { settingsDialog_->raise(); settingsDialog_->activateWindow(); return; }
    captureWindows(preferences_); SettingsDialog dialog(preferences_,*actions_,[this](const auto &p) { return applyPreferences(p); },[this] { clearHistory(); },QApplication::activeWindow()?QApplication::activeWindow():&window_);
    settingsDialog_=&dialog; dialog.exec(); settingsDialog_.clear();
}
void SessionController::rebuildRecent()
{
    if(recent_->isVisible()) return;
    recent_->clear(); QList<QAction *> emptyActions; int index=0;
    for(const auto &entry:history_.entries()) {
        if(index>=10) break; const QFileInfo file(entry.path); const auto path=entry.path; auto *action=recent_->addAction(QStringLiteral("%1. %2").arg(index+1).arg(file.fileName())); action->setToolTip(path+(file.isFile()?entry.matchesFile()?QString():QStringLiteral("\n文件已变化，旧断点不会恢复"):QStringLiteral("\n文件不存在"))); action->setEnabled(file.isFile());
        connect(action,&QAction::triggered,this,[this,path] { playlist_->addFiles({path},true); }); if(index<5) emptyActions.append(action); ++index;
    }
    if(!index) { auto *action=recent_->addAction(QStringLiteral("暂无最近文件")); action->setEnabled(false); }
    recent_->setEnabled(index>0); surface_->setRecentActions(currentPath_.isEmpty()?emptyActions:QList<QAction *>());
}
void SessionController::clearHistory() { recordCurrent_=false; if(!currentPath_.isEmpty()) suppressedPaths_.insert(mediaPathKey(currentPath_)); dismissResume(); history_.clear(); rebuildRecent(); surface_->showOsd("history",QStringLiteral("最近文件和播放进度已清除"),2500); }
void SessionController::flushProgress(bool ended)
{
    if(!preferences_.recordHistory || !recordCurrent_ || awaitingResume_ || resumeSeek_ || currentPath_.isEmpty() || currentPath_!=player_.path()) return;
    if(player_.state()!=Player::State::Loaded && player_.state()!=Player::State::Ended) return;
    history_.remember(currentPath_,player_.position(),player_.duration(),ended);
}
void SessionController::prepareFile()
{
    const auto key=mediaPathKey(currentPath_); if(!player_.automaticOpen()) suppressedPaths_.remove(key); recordCurrent_=preferences_.recordHistory && !suppressedPaths_.contains(key);
    resumeEntry_=history_.find(currentPath_); awaitingResume_=player_.resumeAllowed() && preferences_.recordHistory && preferences_.resumePolicy!=2 && resumeEntry_ && resumeEntry_->resumable();
    if(awaitingResume_) player_.setInitialPaused(true); recentTimer_.start(0);
}
void SessionController::tryResume()
{
    if(!awaitingResume_ || player_.state()!=Player::State::Loaded || player_.recovering() || !player_.seekable() || player_.lastPosition()<=0 || player_.duration()<=0 || player_.tracks().isEmpty()) return;
    bool video=false; for(const auto &track:player_.tracks()) video|=track.type=="video"; if(video && !video_->canCapture()) return;
    if(!resumeEntry_ || !resumeEntry_->resumable() || resumeEntry_->position>=player_.duration()-30) { chooseResume(false); return; }
    if(preferences_.resumePolicy==1) { chooseResume(true); return; }
    if(prompt_->isVisible()) { if(!resumeTimer_.isActive()) resumeTimer_.start(); return; }
    const int seconds=int(resumeEntry_->position); promptText_->setText(QStringLiteral("继续上次 %1:%2:%3？\n8 秒未选择将从头播放。").arg(seconds/3600,2,10,QChar('0')).arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0')));
    arrangePrompt(); prompt_->show(); prompt_->raise(); surface_->setProperty("resumePending",true); prompt_->findChild<QPushButton *>("resumeContinueButton")->setFocus(); resumeTimer_.start();
}
void SessionController::arrangePrompt() { prompt_->setFixedWidth(qMin(480,qMax(220,surface_->width()-48))); prompt_->adjustSize(); prompt_->move((surface_->width()-prompt_->width())/2,surface_->overlayTop()); }
void SessionController::dismissResume()
{
    awaitingResume_=false; resumeSeek_.reset(); resumeTimer_.stop(); surface_->setProperty("resumePending",false); const bool focused=prompt_->isAncestorOf(QApplication::focusWidget()); prompt_->hide(); if(focused) video_->setFocus();
}
void SessionController::chooseResume(bool resume)
{
    if(!awaitingResume_ || player_.state()!=Player::State::Loaded) return;
    const double position=resume && resumeEntry_ && resumeEntry_->resumable()?resumeEntry_->position:0; dismissResume(); choosing_=true; player_.seek(position); choosing_=false;
    if(position>0) { resumeSeek_=position; const auto generation=player_.generation(); QTimer::singleShot(5000,this,[this,generation] { if(resumeSeek_ && generation==player_.generation()) { resumeSeek_.reset(); surface_->showOsd("history",QStringLiteral("断点定位未完成，请重新定位或播放。"),4000); } }); }
    else player_.setPaused(false);
    video_->setFocus();
}
