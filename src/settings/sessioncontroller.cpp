#include "sessioncontroller.h"
#include "settingsdialog.h"
#include "player.h"
#include "mainwindow.h"
#include "controlbar.h"
#include "mediacontrols.h"
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
    : QObject(&window),player_(player),window_(window),settings_(settings),history_(history),preferences_(settings.load()),savedPreferences_(preferences_)
{
    setObjectName("sessionController"); actions_=window.findChild<PlayerActions *>(); playlist_=window.findChild<PlaylistController *>(); host_=window.findChild<PlaylistHost *>(); surface_=window.findChild<VideoSurface *>(); video_=window.findChild<VideoWidget *>();
    controls_ = window.findChild<ControlBar *>();
    recent_=new QMenu(QStringLiteral("最近文件"),&window); recent_->setObjectName("recentFilesMenu"); auto *file=window.findChild<QMenu *>("fileMenu"); file->insertMenu(actions_->get(PlayerActions::Quit),recent_); file->insertAction(actions_->get(PlayerActions::Quit),actions_->get(PlayerActions::ClearHistory));
    window.findChild<QMenu *>("viewMenu")->addAction(actions_->get(PlayerActions::Settings));
    window.findChild<QMenu *>("videoContextMenu")->addAction(actions_->get(PlayerActions::Settings));
    actions_->get(PlayerActions::Settings)->setEnabled(true); actions_->get(PlayerActions::ClearHistory)->setEnabled(true);
    connect(actions_->get(PlayerActions::Settings),&QAction::triggered,this,&SessionController::showSettings); connect(actions_->get(PlayerActions::ClearHistory),&QAction::triggered,this,&SessionController::clearHistory);
    auto *skipMenu = window.findChild<QMenu *>("playMenu")->addMenu(QStringLiteral("跳略播放"));
    skipMenu->setObjectName("skipPlaybackMenu");
    const auto addSkipToggle = [this,skipMenu](PlayerActions::Id id, bool Preferences::*field) {
        auto *action = actions_->get(id);
        action->setEnabled(true);
        skipMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, field](bool enabled) {
            preferences_.*field = enabled;
            updateSkipSettings();
            settingsTimer_.start();
        });
    };
    addSkipToggle(PlayerActions::SkipToggle, &Preferences::skipEnabled);
    addSkipToggle(PlayerActions::SkipIntro, &Preferences::introEnabled);
    addSkipToggle(PlayerActions::SkipOutro, &Preferences::outroEnabled);
    skipMenu->addSeparator();
    auto *skipSettings = actions_->get(PlayerActions::SkipSettings);
    skipSettings->setEnabled(true);
    skipMenu->addAction(skipSettings);
    connect(skipSettings, &QAction::triggered, this, [this] { showSettingsPage(true); });
    prompt_=new QWidget(surface_); prompt_->setObjectName("resumePrompt"); prompt_->setAutoFillBackground(true); auto *layout=new QVBoxLayout(prompt_);
    promptText_=new QLabel; promptText_->setWordWrap(true); promptText_->setTextFormat(Qt::PlainText); layout->addWidget(promptText_);
    auto *row=new QHBoxLayout; auto *resume=new QPushButton(QStringLiteral("继续")),*start=new QPushButton(QStringLiteral("从头播放")); resume->setObjectName("resumeContinueButton"); resume->setDefault(true); start->setObjectName("resumeStartButton"); row->addWidget(resume); row->addWidget(start); layout->addLayout(row); prompt_->hide();
    connect(surface_,&VideoSurface::overlayTopChanged,this,&SessionController::arrangePrompt);
    connect(resume,&QPushButton::clicked,this,[this] { chooseResume(true); }); connect(start,&QPushButton::clicked,this,[this] { chooseResume(false); });
    startTimer_.setSingleShot(true);
    connect(&startTimer_, &QTimer::timeout, this, &SessionController::startTimedOut);
    recentTimer_.setSingleShot(true); connect(&recentTimer_,&QTimer::timeout,this,&SessionController::rebuildRecent);
    connect(&history_,&HistoryStore::changed,this,[this] { recentTimer_.start(0); }); connect(recent_,&QMenu::aboutToShow,this,&SessionController::rebuildRecent);
    connect(&history_,&HistoryStore::error,this,[this](const QString &message) { surface_->showOsd("storage",message,5000); });
    progressTimer_.setInterval(5000); connect(&progressTimer_,&QTimer::timeout,this,[this] { flushProgress(); }); progressTimer_.start();
    settingsTimer_.setSingleShot(true); settingsTimer_.setInterval(700); connect(&settingsTimer_,&QTimer::timeout,this,&SessionController::saveWindows);
    window.installEventFilter(this); host_->window()->installEventFilter(this); surface_->installEventFilter(this);
    auto *mediaControls=window.findChild<MediaControls *>();
    connect(host_,&PlaylistHost::changed,this,&SessionController::scheduleSave);
    connect(playlist_,&PlaylistController::playbackOrderChanged,this,&SessionController::scheduleSave);
    connect(mediaControls,&MediaControls::alwaysOnTopChanged,this,&SessionController::scheduleSave);
    connect(video_,&VideoWidget::viewChanged,this,&SessionController::scheduleSave);
    connect(&player_,&Player::preferenceTargetChanged,this,&SessionController::scheduleSave);
    connect(&player_,&Player::volumeChanged,this,&SessionController::scheduleSave);
    connect(&player_,&Player::mutedChanged,this,&SessionController::scheduleSave);
    connect(&player_, &Player::detailsChanged, this, [this] {
        if (player_.state() == Player::State::Loaded && !player_.recovering()) {
            const bool loop = player_.loopActive();
            if (loopWasActive_ && !loop && skip_.inOutro(player_.position())) skip_.watchingOutro = true;
            loopWasActive_ = loop;
        }
        tryStart();
    });
    connect(actions_->get(PlayerActions::StandardMode),&QAction::toggled,this,&SessionController::scheduleSave);
    connect(&player_,&Player::fileOpening,this,[this] { flushProgress(); resetStart(); });
    connect(&player_,&Player::stopping,this,[this] { flushProgress(); resetStart(); recordCurrent_=false; });
    connect(&player_,&Player::mediaChanged,this,[this](Player::State state,const QString &path,const QString &) {
        if(state==Player::State::Loading && !player_.lastLoadWasRecovery()) { currentPath_=path; prepareFile(); }
        if (state == Player::State::Loaded && !player_.lastLoadWasRecovery()) {
            if (startState_ != StartState::Ready) armStartTimer(2000);
            tryStart();
            flushProgress();
        }
        if(state==Player::State::Error || state==Player::State::Idle) { resetStart(); recordCurrent_=false; }
        if(state==Player::State::Idle && path.isEmpty()) currentPath_.clear();
    });
    playlist_->setCompletionHandler([session = QPointer(this), playlist = QPointer(playlist_)](const Player::PlaybackIdentity &playback) {
        if (session) session->onPlaybackCompleted(playback);
        else if (playlist) playlist->advanceAfterCompletion(playback);
    });
    const auto mediaBoundsChanged = [this] { updateSkipBounds(); tryStart(); };
    connect(&player_, &Player::durationChanged, this, mediaBoundsChanged);
    connect(&player_, &Player::seekableChanged, this, mediaBoundsChanged);
    connect(&player_, &Player::audioAvailableChanged, this, &SessionController::tryStart);
    connect(video_, &QOpenGLWidget::frameSwapped, this, &SessionController::tryStart);
    connect(&player_, &Player::pausedChanged, this, [this](bool paused) { if (paused) flushProgress(); });
    connect(&player_, &Player::startPlaybackRequested, this, [this] {
        if (startState_ == StartState::Deciding) chooseResume(false);
    });
    connect(&player_, &Player::startPositionResolved, this, [this](const auto &playback, auto reason, bool succeeded) {
        if (startState_ != StartState::Seeking || playback != startPlayback_ || reason != startReason_) return;
        if (!succeeded) startTimedOut();
        else {
            if (skip_.inOutro(player_.position())) {
                skip_.watchingOutro = true;
                if (reason == Player::StartPosition::Resume)
                    surface_->showOsd("skip", QStringLiteral("断点已在片尾，本次可继续观看片尾。"), 3500);
            }
            releaseStart();
        }
    });
    connect(&player_, &Player::seekRequested, this, [this](double target) {
        skip_.watchingOutro = skip_.inOutro(target);
        if (startState_ == StartState::Ready) return;
        hideResumePrompt();
        startState_ = StartState::Seeking;
        startReason_ = Player::StartPosition::User;
        startTarget_ = target;
        armStartTimer(5000);
    });
    connect(&player_, &Player::seekSettled, this, [this](double position) {
        skip_.watchingOutro = skip_.inOutro(position);
    });
    connect(&player_, &Player::restored, this, [this] {
        updateSkipBounds();
        if (startState_ == StartState::Seeking) {
            if ((startReason_ == Player::StartPosition::SkipIntro && skip_.introSeconds == 0)
                || (startReason_ == Player::StartPosition::Resume && !preferences_.recordHistory))
                chooseResume(false);
            else startAt(startTarget_, startReason_);
        } else if (startState_ == StartState::Deciding) {
            armStartTimer(prompt_->isVisible() ? 8000 : 2000);
            tryStart();
        }
    });
    connect(&player_, &Player::playbackAdvanced, this, &SessionController::onPlaybackAdvanced);
    connect(qApp,&QGuiApplication::screenRemoved,this,[this] { QTimer::singleShot(0,this,&SessionController::ensureVisibleWindows); });
    settingsNeedWrite_=settings_.needsWrite();
    applySettings();
    mediaControls->setAlwaysOnTop(preferences_.alwaysOnTop);
    if(preferences_.mainGeometry.isValid()) window_.setGeometry(preferences_.mainGeometry);
    if(preferences_.playlistGeometry.isValid()) host_->window()->setGeometry(preferences_.playlistGeometry);
    ensureVisibleWindows(); if(preferences_.maximized) window_.setWindowState(Qt::WindowMaximized);
    host_->restore(preferences_.detached,preferences_.playlistVisible,preferences_.dockWidth);
    applyPlaybackSettings(preferences_);
    video_->restoreView(preferences_.viewScale, preferences_.viewFocus);
    rebuildRecent(); restoring_=false;
    if(settingsNeedWrite_) settingsTimer_.start();
    const auto message=settings_.error().isEmpty()?history_.loadError():settings_.error(); if(!message.isEmpty()) QTimer::singleShot(0,this,[this,message] { surface_->showOsd("storage",message,5000); });
}
void SessionController::captureRuntime(Preferences &p) const
{
    if(!window_.isMinimized()) {
        p.maximized=window_.restoreMaximized();
        p.mainGeometry=window_.windowedGeometry();
    }
    auto *list=host_->window(); const auto normal=list->normalGeometry(); p.playlistGeometry=list->isMinimized() || list->isMaximized()?normal:list->geometry();
    p.detached=host_->detached(); p.playlistVisible=host_->preferredVisible(); p.dockWidth=host_->dockWidth();
    p.alwaysOnTop=window_.windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    p.volume=player_.volumeTarget(); p.muted=player_.muteTarget(); p.standardMode=actions_->get(PlayerActions::StandardMode)->isChecked();
    p.subtitleScale=player_.propertyTarget("sub-scale").toDouble();
    p.speed=player_.propertyTarget("speed").toDouble();
    p.pitchCorrection=player_.propertyTarget("audio-pitch-correction").toBool();
    p.subtitleVisible=player_.propertyTarget("sub-visibility").toBool();
    p.audioDelay=player_.propertyTarget("audio-delay").toDouble();
    p.subtitleDelay=player_.propertyTarget("sub-delay").toDouble();
    p.viewScale=video_->zoomScale();
    p.viewFocus=video_->viewFocus();
    p.repeatMode=playlist_->repeatMode();
    p.shuffleEnabled=playlist_->shuffleEnabled();
}
void SessionController::applySettings()
{
    actions_->setSeekSteps(preferences_.seekSmallSeconds, preferences_.seekMediumSeconds, preferences_.seekLargeSeconds);
    updateSkipSettings();
    window_.findChild<CaptureService *>()->setDirectory(preferences_.captureDirectory); player_.setTrackLanguages(preferences_.audioLanguages,preferences_.subtitleLanguages);
    video_->setInputMode(preferences_.standardMode?VideoWidget::InputMode::Standard:VideoWidget::InputMode::Feature);
    actions_->get(preferences_.standardMode?PlayerActions::StandardMode:PlayerActions::FeatureMode)->setChecked(true);
    const auto ignored=actions_->applyBindings(preferences_.shortcuts); if(!ignored.isEmpty()) QTimer::singleShot(0,this,[this] { surface_->showOsd("settings",QStringLiteral("已保留有效的自定义快捷键。无效或冲突的按键已调整，可在设置中查看当前绑定。"),5000); });
}
void SessionController::applyPlaybackSettings(const Preferences &p)
{
    playlist_->setPlaybackOrder(p.repeatMode, p.shuffleEnabled);
    player_.setAudioPreferences(p.volume, p.muted);
    player_.setMediaProperty("speed", p.speed);
    player_.setMediaProperty("audio-pitch-correction", p.pitchCorrection);
    player_.setMediaProperty("sub-visibility", p.subtitleVisible);
    player_.setMediaProperty("audio-delay", p.audioDelay);
    player_.setMediaProperty("sub-delay", p.subtitleDelay);
    player_.setMediaProperty("sub-scale", p.subtitleScale);
}

bool SessionController::applyDialogChanges(const Preferences &draft, const SettingsEdits &edits)
{
    auto p = edits.resetAll ? SettingsStore::defaults() : preferences_;
    p.shortcuts = preferences_.shortcuts;
    if(edits.captureDirectory) p.captureDirectory=draft.captureDirectory;
    if(edits.audioLanguages) p.audioLanguages=draft.audioLanguages;
    if(edits.subtitleLanguages) p.subtitleLanguages=draft.subtitleLanguages;
    if(edits.recordHistory) p.recordHistory=draft.recordHistory;
    if(edits.resumePolicy) p.resumePolicy=draft.resumePolicy;
    if(edits.seekSmallSeconds) p.seekSmallSeconds=draft.seekSmallSeconds;
    if(edits.seekMediumSeconds) p.seekMediumSeconds=draft.seekMediumSeconds;
    if(edits.seekLargeSeconds) p.seekLargeSeconds=draft.seekLargeSeconds;
    if (edits.skipEnabled) p.skipEnabled = draft.skipEnabled;
    if (edits.introEnabled) p.introEnabled = draft.introEnabled;
    if (edits.outroEnabled) p.outroEnabled = draft.outroEnabled;
    if (edits.introSeconds) p.introSeconds = draft.introSeconds;
    if (edits.outroSeconds) p.outroSeconds = draft.outroSeconds;
    for(const auto &id : edits.shortcuts) {
        const auto binding = draft.shortcuts.constFind(id);
        if(binding == draft.shortcuts.cend()) p.shortcuts.remove(id);
        else p.shortcuts.insert(id, binding.value());
    }
    return commitPreferences(std::move(p), edits.resetAll);
}

bool SessionController::applyPreferences(Preferences p)
{
    return commitPreferences(std::move(p), false);
}

bool SessionController::commitPreferences(Preferences p, bool resetAll)
{
    if(!resetAll) captureRuntime(p);
    if((settingsNeedWrite_ || p!=savedPreferences_) && !settings_.save(p)) { settingsError(settings_.error()); return false; }
    const bool wasRecording=preferences_.recordHistory;
    settingsTimer_.stop();
    preferences_=std::move(p);
    savedPreferences_=preferences_;
    settingsNeedWrite_=false; settingsErrorReported_=false;
    restoring_=true;
    applySettings();
    if(wasRecording && !preferences_.recordHistory) {
        recordCurrent_ = false;
        discardResume();
    }
    else if(!wasRecording && preferences_.recordHistory && !currentPath_.isEmpty() && !suppressedPaths_.contains(mediaPathKey(currentPath_))) recordCurrent_=true;
    if(resetAll) {
        applyPlaybackSettings(preferences_);
        video_->resetView();
        window_.findChild<MediaControls *>()->setAlwaysOnTop(preferences_.alwaysOnTop);
        window_.showNormal();
        const auto area=window_.screen()->availableGeometry();
        const auto size=QSize(1120,720).boundedTo(area.size()*.9).expandedTo(window_.minimumSize());
        window_.setGeometry(QRect(area.center()-QPoint(size.width()/2,size.height()/2),size));
        host_->restore(preferences_.detached,preferences_.playlistVisible,preferences_.dockWidth);
        host_->window()->resize(320,560);
        host_->window()->move(area.center()-QPoint(160,280));
        ensureVisibleWindows();
    }
    restoring_=false;
    scheduleSave();
    return true;
}
void SessionController::settingsError(const QString &message, bool runtimeChanged)
{
    if(settingsErrorReported_) return;
    settingsErrorReported_=true;
    const auto text=runtimeChanged ? message+QStringLiteral("\n未保存的偏好在重启后会丢失。") : message;
    surface_->showOsd("storage",text,5000);
}
void SessionController::scheduleSave()
{
    if(restoring_) return;
    auto current=preferences_;
    captureRuntime(current);
    const bool changed=current!=preferences_;
    if(changed) preferences_=std::move(current);
    if(changed || (!settingsTimer_.isActive() && (settingsNeedWrite_ || preferences_!=savedPreferences_))) settingsTimer_.start();
}
void SessionController::saveWindows()
{
    if(restoring_) return;
    settingsTimer_.stop();
    captureRuntime(preferences_);
    if(!settingsNeedWrite_ && preferences_==savedPreferences_) return;
    if(!settings_.save(preferences_)) { settingsError(settings_.error(),preferences_!=savedPreferences_); return; }
    savedPreferences_=preferences_;
    settingsNeedWrite_=false;
    settingsErrorReported_=false;
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
    if((object==&window_ || object==host_->window()) && (event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::WindowStateChange)) scheduleSave();
    return QObject::eventFilter(object,event);
}
void SessionController::showSettings()
{
    showSettingsPage(false);
}

void SessionController::showSettingsPage(bool skipPage)
{
    if (settingsDialog_) {
        if (skipPage) settingsDialog_->showSkipPage();
        settingsDialog_->raise(); settingsDialog_->activateWindow(); return;
    }
    scheduleSave();
    SettingsDialog dialog(preferences_,*actions_,[this](const auto &draft,const auto &edits) {
        return applyDialogChanges(draft,edits);
    },[this] { clearHistory(); },QApplication::activeWindow()?QApplication::activeWindow():&window_);
    if (skipPage) dialog.showSkipPage();
    settingsDialog_=&dialog; dialog.exec(); settingsDialog_.clear();
}
void SessionController::rebuildRecent()
{
    if(recent_->isVisible()) return;
    recent_->clear(); int index=0;
    for(const auto &entry:history_.entries()) {
        if(index>=10) break; const QFileInfo file(entry.path); const auto path=entry.path; auto *action=recent_->addAction(QStringLiteral("%1. %2").arg(index+1).arg(file.fileName())); action->setToolTip(path+(file.isFile()?entry.matchesFile()?QString():QStringLiteral("\n文件已变化，旧断点不会恢复"):QStringLiteral("\n文件不存在"))); action->setEnabled(file.isFile());
        connect(action,&QAction::triggered,this,[this,path] { playlist_->addFiles({path},true); }); ++index;
    }
    if(!index) { auto *action=recent_->addAction(QStringLiteral("暂无最近文件")); action->setEnabled(false); }
    recent_->setEnabled(index>0);
}
void SessionController::clearHistory()
{
    recordCurrent_ = false;
    if (!currentPath_.isEmpty()) suppressedPaths_.insert(mediaPathKey(currentPath_));
    discardResume();
    history_.clear(); rebuildRecent(); surface_->showOsd("history",QStringLiteral("最近文件和播放进度已清除"),2500);
}
void SessionController::flushProgress()
{
    if (player_.state() == Player::State::Loaded) recordProgress(false);
}

void SessionController::onPlaybackCompleted(const Player::PlaybackIdentity &playback)
{
    if (!player_.ownsCompletion(playback)) return;
    resetStart();
    const bool recorded = recordProgress(true);
    recordCurrent_ = false;
    if (!recorded) {
        playlist_->advanceAfterCompletion(playback);
        return;
    }
    history_.afterPendingWrite([playlist = QPointer(playlist_), playback] {
        if (playlist) playlist->advanceAfterCompletion(playback);
    });
}

bool SessionController::recordProgress(bool ended)
{
    if(!preferences_.recordHistory || !recordCurrent_ || startState_ != StartState::Ready || currentPath_.isEmpty() || currentPath_!=player_.path()) return false;
    return history_.remember(currentPath_,player_.position(),player_.duration(),ended);
}
void SessionController::prepareFile()
{
    const auto key = mediaPathKey(currentPath_);
    if (!player_.automaticOpen()) suppressedPaths_.remove(key);
    recordCurrent_ = preferences_.recordHistory && !suppressedPaths_.contains(key);
    if (player_.resumeAllowed() && preferences_.recordHistory && preferences_.resumePolicy != 2) {
        resumeEntry_ = history_.find(currentPath_);
        if (resumeEntry_ && !resumeEntry_->resumable()) resumeEntry_.reset();
    }
    if (preferences_.skipEnabled && QFileInfo(currentPath_).isAbsolute()) {
        if (preferences_.introEnabled) skip_.introSeconds = preferences_.introSeconds;
        if (preferences_.outroEnabled) skip_.outroSeconds = preferences_.outroSeconds;
    }
    if (resumeEntry_ || skip_.introSeconds > 0) {
        startState_ = StartState::Deciding;
        player_.holdStart(true);
    }
}

void SessionController::updateSkipSettings()
{
    actions_->get(PlayerActions::SkipToggle)->setChecked(preferences_.skipEnabled);
    auto *intro = actions_->get(PlayerActions::SkipIntro);
    intro->setChecked(preferences_.introEnabled);
    intro->setText(QStringLiteral("跳过片头（%1 秒）").arg(preferences_.introSeconds));
    auto *outro = actions_->get(PlayerActions::SkipOutro);
    outro->setChecked(preferences_.outroEnabled);
    outro->setText(QStringLiteral("跳过片尾（%1 秒）").arg(preferences_.outroSeconds));
    if (preferences_.skipEnabled) return;

    skip_ = {};
    if (startState_ == StartState::Seeking && startReason_ == Player::StartPosition::SkipIntro)
        chooseResume(false);
    else if (startState_ == StartState::Deciding && !resumeEntry_ && !player_.recovering()) {
        if (player_.state() == Player::State::Loaded) releaseStart();
        else player_.setInitialPaused(false);
    }
}

void SessionController::updateSkipBounds()
{
    if (player_.state() != Player::State::Loaded || player_.recovering()) return;
    if (skip_.updateBounds(player_.duration(), player_.seekable(), player_.position()) == SkipPolicy::Bounds::Invalid)
        surface_->showOsd("skip", QStringLiteral("跳略秒数超出本文件可用区间，已按完整时长播放。"), 4000);
}

void SessionController::onPlaybackAdvanced(const Player::PlaybackIdentity &playback, double before, double position)
{
    if (startState_ != StartState::Ready || controls_->seekPreviewActive() || !skip_.crossedOutro(before, position)) return;
    if (!player_.completeOutro(playback)) skip_.watchingOutro = true;
}

void SessionController::tryStart()
{
    if (startState_ != StartState::Deciding || player_.state() != Player::State::Loaded
        || player_.recovering() || player_.playbackIdentity() != startPlayback_) return;
    if (!resumeEntry_ && skip_.introSeconds == 0) { releaseStart(); return; }
    if (!player_.canSeek()) return;
    if (!resumeEntry_ || resumeEntry_->position >= player_.duration() - 30) {
        chooseResume(false);
        return;
    }
    if (preferences_.resumePolicy == 1) { chooseResume(true); return; }
    if (prompt_->isVisible() || player_.tracks().isEmpty()) return;
    for (const auto &track : player_.tracks()) if (track.type == "video" && !video_->canCapture()) return;
    const int seconds = int(resumeEntry_->position);
    promptText_->setText(QStringLiteral("继续上次 %1:%2:%3？\n8 秒未选择将从头播放。").arg(seconds/3600,2,10,QChar('0')).arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0')));
    arrangePrompt(); prompt_->show(); prompt_->raise(); surface_->setProperty("resumePending",true); prompt_->findChild<QPushButton *>("resumeContinueButton")->setFocus();
    armStartTimer(8000);
}

void SessionController::armStartTimer(int milliseconds)
{
    startPlayback_ = player_.playbackIdentity();
    startTimer_.start(milliseconds);
}

void SessionController::startTimedOut()
{
    if (startState_ == StartState::Ready || player_.playbackIdentity() != startPlayback_
        || player_.state() != Player::State::Loaded || player_.recovering()) return;
    if (startState_ == StartState::Deciding && prompt_->isVisible()) { chooseResume(false); return; }
    if (startState_ == StartState::Deciding) {
        releaseStart(QStringLiteral("无法确认媒体起点，已按原起点播放。"));
    } else if (startReason_ == Player::StartPosition::SkipIntro || startReason_ == Player::StartPosition::Resume) {
        surface_->showOsd("skip", QStringLiteral("起点定位未完成，已尝试从头播放。"), 4000);
        startAt(0, Player::StartPosition::Beginning);
    } else {
        releaseStart(QStringLiteral("起点定位未完成，已恢复播放。"));
    }
}

void SessionController::startAt(double seconds, Player::StartPosition reason)
{
    resumeEntry_.reset();
    hideResumePrompt();
    startState_ = StartState::Seeking;
    startReason_ = reason;
    startTarget_ = seconds;
    armStartTimer(5000);
    if (!player_.seekToStart(seconds, reason)) startTimedOut();
}

void SessionController::arrangePrompt() { prompt_->setFixedWidth(qMin(480,qMax(220,surface_->width()-48))); prompt_->adjustSize(); prompt_->move((surface_->width()-prompt_->width())/2,surface_->overlayTop()); }
void SessionController::hideResumePrompt()
{
    surface_->setProperty("resumePending", false);
    const bool focused = prompt_->isAncestorOf(QApplication::focusWidget());
    prompt_->hide();
    if (focused) video_->setFocus();
}

void SessionController::resetStart()
{
    startTimer_.stop();
    hideResumePrompt();
    startState_ = StartState::Ready;
    resumeEntry_.reset();
    player_.holdStart(false);
    skip_ = {};
    loopWasActive_ = false;
}

void SessionController::releaseStart(const QString &message)
{
    startTimer_.stop();
    hideResumePrompt();
    startState_ = StartState::Ready;
    resumeEntry_.reset();
    player_.cancelStartSeek();
    player_.holdStart(false);
    if (!message.isEmpty()) surface_->showOsd("skip", message, 4000);
    player_.setPaused(false);
}

void SessionController::discardResume()
{
    resumeEntry_.reset();
    if (startState_ == StartState::Seeking && startReason_ == Player::StartPosition::Resume)
        chooseResume(false);
    else if (startState_ == StartState::Deciding) tryStart();
}

void SessionController::chooseResume(bool resume)
{
    if (startState_ == StartState::Ready || player_.state() != Player::State::Loaded || player_.recovering()) return;
    if (resume && resumeEntry_ && resumeEntry_->resumable() && resumeEntry_->position < player_.duration() - 30) {
        startAt(resumeEntry_->position, Player::StartPosition::Resume);
    } else if (skip_.introSeconds > 0 && player_.canSeek() && !player_.loopActive()) {
        startAt(skip_.introSeconds, Player::StartPosition::SkipIntro);
    } else if (startState_ == StartState::Seeking) {
        startAt(0, Player::StartPosition::Beginning);
    } else {
        releaseStart();
    }
}
