#include "mainwindow.h"
#include "controlbar.h"
#include "playeractions.h"
#include "playbackdialogs.h"
#include "theme.h"
#include "videosurface.h"
#include "videowidget.h"
#include "playlistcontroller.h"
#include "playlistpanel.h"
#include "playlisthost.h"
#include "folderscanner.h"
#include "mediacontrols.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QKeyEvent>
#include <QSlider>
#include <QSplitter>
#include <QListView>
#include <QCloseEvent>
#include <QUrl>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
int wholeSeconds(double value)
{
    return std::isfinite(value) ? int(std::clamp(value, 0.0, double(std::numeric_limits<int>::max()))) : 0;
}
}

MainWindow::MainWindow(Player &player, QWidget *parent) : QMainWindow(parent), player_(player)
{
    auto uiFont = font();
    if (uiFont.pointSizeF() < 10) { uiFont.setPointSizeF(10); setFont(uiFont); }
    auto *theme = new Theme::Controller(*qApp, this);
    auto *central = new QWidget; setCentralWidget(central);
    auto *layout = new QVBoxLayout(central); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    video_ = new VideoWidget(player.handle()); video_->setObjectName("videoWidget");
    video_->setAccessibleName(QStringLiteral("视频画面"));
    splitter_ = new QSplitter(Qt::Horizontal, central); splitter_->setObjectName("playerSplitter"); layout->addWidget(splitter_);
    splitter_->setChildrenCollapsible(false);
    surface_ = new VideoSurface(video_, splitter_); splitter_->addWidget(surface_);
    actions_ = new PlayerActions(*this, *video_, *surface_);
    controls_ = new ControlBar(*actions_, surface_); surface_->setControls(controls_);
    setupPlaylist();
    setAcceptDrops(true); surface_->setMouseTracking(true);
    hideTimer_.setSingleShot(true); hideTimer_.setInterval(2500);
    connect(&hideTimer_, &QTimer::timeout, this, &MainWindow::hideControls);
    qApp->installEventFilter(this);
    connect(actions_, &PlayerActions::invoked, this, &MainWindow::revealControls);
    connect(theme, &Theme::Controller::applied, this, [this] { actions_->refreshIcons(); controls_->refreshIcons(); });

    const auto action = [this](PlayerActions::Id id) { return actions_->get(id); };
    connect(action(PlayerActions::Open), &QAction::triggered, this, &MainWindow::openFile);
    connect(action(PlayerActions::Retry), &QAction::triggered, this, [this] { if (playlist_->model().currentId()) playlist_->replay(); else if (!path_.isEmpty()) player_.openFile(path_); });
    connect(action(PlayerActions::PlayPause), &QAction::triggered, this, [this] {
        if (state_ == Player::State::Loaded) player_.togglePaused();
        else if (playlist_->model().rowCount()) playlist_->play(playlist_->model().currentId() ? playlist_->model().currentId() : panel_->selectedId() ? panel_->selectedId() : playlist_->model().idAt(0));
        else if (!path_.isEmpty()) player_.openFile(path_);
    });
    connect(action(PlayerActions::ResetView), &QAction::triggered, video_, &VideoWidget::resetView);
    connect(action(PlayerActions::Stop), &QAction::triggered, &player_, &Player::stop);
    for (auto [id, seconds] : {std::pair{PlayerActions::BackSmall, -5}, {PlayerActions::ForwardSmall, 5},
            {PlayerActions::BackMedium, -30}, {PlayerActions::ForwardMedium, 30},
            {PlayerActions::BackLarge, -60}, {PlayerActions::ForwardLarge, 60}})
        connect(action(id), &QAction::triggered, this, [this, seconds] { player_.seekRelative(seconds); });
    connect(action(PlayerActions::SeekStart), &QAction::triggered, this, [this] { player_.seek(0); });
    connect(action(PlayerActions::SeekEnd), &QAction::triggered, this, [this] { player_.seek(player_.lastPosition()); });
    connect(action(PlayerActions::SeekGoto), &QAction::triggered, this, &MainWindow::gotoTime);
    connect(action(PlayerActions::VolumeUp), &QAction::triggered, this, [this] { player_.setVolume(player_.volumeTarget() + 5); });
    connect(action(PlayerActions::VolumeDown), &QAction::triggered, this, [this] { player_.setVolume(player_.volumeTarget() - 5); });
    connect(action(PlayerActions::Mute), &QAction::triggered, &player_, &Player::toggleMuted);
    connect(action(PlayerActions::InputMode), &QAction::triggered, this, [this] {
        actions_->get(actions_->get(PlayerActions::FeatureMode)->isChecked() ? PlayerActions::StandardMode : PlayerActions::FeatureMode)->trigger();
    });
    connect(action(PlayerActions::ZoomIn), &QAction::triggered, this, [this] { video_->zoomSteps(1); });
    connect(action(PlayerActions::ZoomOut), &QAction::triggered, this, [this] { video_->zoomSteps(-1); });
    connect(action(PlayerActions::Shortcuts), &QAction::triggered, this, &MainWindow::showShortcuts);
    connect(action(PlayerActions::Fullscreen), &QAction::triggered, this, &MainWindow::toggleFullscreen);
    connect(action(PlayerActions::Escape), &QAction::triggered, this, [this] {
        if (QApplication::activeWindow() == playlistHost_->window()) return;
        if (controls_->cancelSeekPreview() || video_->cancelInput()) { revealControls(); return; }
        if (playlistHost_->dismissOverlay()) return;
        if (isFullScreen()) toggleFullscreen();
    });
    connect(action(PlayerActions::FeatureMode), &QAction::triggered, video_, [this] {
        video_->setInputMode(VideoWidget::InputMode::Feature);
        surface_->showOsd("mode", QStringLiteral("缩放拖动：滚轮放大，左键拖动"), 2000);
    });
    connect(action(PlayerActions::StandardMode), &QAction::triggered, video_, [this] {
        video_->setInputMode(VideoWidget::InputMode::Standard);
        surface_->showOsd("mode", QStringLiteral("常规播放：滚轮调音量，单击播放或暂停"), 2000);
    });
    connect(action(PlayerActions::Quit), &QAction::triggered, this, &QWidget::close);
    connect(action(PlayerActions::About), &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("关于 zopana"), QStringLiteral("zopana\n本地视频播放与画面细节观察\nQt Widgets · libmpv\n图标：Lucide（ISC / Feather MIT）\n完整图标许可见“帮助 → 第三方许可”。"));
    });
    connect(action(PlayerActions::Licenses), &QAction::triggered, this, &MainWindow::showLicenses);

    auto *file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    file->setObjectName("fileMenu");
    file->addAction(action(PlayerActions::Open)); file->addAction(action(PlayerActions::Add)); file->addAction(action(PlayerActions::AddFolder));
    file->addSeparator(); file->addAction(action(PlayerActions::Quit));
    auto *play = menuBar()->addMenu(QStringLiteral("播放(&P)"));
    play->setObjectName("playMenu");
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::Previous, PlayerActions::Next, PlayerActions::BackSmall, PlayerActions::ForwardSmall,
            PlayerActions::BackMedium, PlayerActions::ForwardMedium, PlayerActions::BackLarge, PlayerActions::ForwardLarge,
            PlayerActions::SeekStart, PlayerActions::SeekEnd, PlayerActions::SeekGoto}) play->addAction(action(id));
    auto *order = play->addMenu(QStringLiteral("播放顺序"));
    for (auto id : {PlayerActions::OrderSequential, PlayerActions::OrderSingle, PlayerActions::OrderList, PlayerActions::OrderRandom}) order->addAction(action(id));
    auto *picture = menuBar()->addMenu(QStringLiteral("画面(&V)"));
    picture->setObjectName("pictureMenu");
    picture->addAction(action(PlayerActions::FeatureMode)); picture->addAction(action(PlayerActions::StandardMode));
    picture->addAction(action(PlayerActions::InputMode));
    picture->addSeparator(); picture->addAction(action(PlayerActions::ZoomIn)); picture->addAction(action(PlayerActions::ZoomOut)); picture->addAction(action(PlayerActions::ResetView));
    auto *audio = menuBar()->addMenu(QStringLiteral("音频(&A)"));
    audio->setObjectName("audioMenu");
    audio->addMenu(controls_->volumeMenu())->setText(QStringLiteral("音量"));
    for (auto id : {PlayerActions::Mute, PlayerActions::VolumeUp, PlayerActions::VolumeDown}) audio->addAction(action(id));
    auto *view = menuBar()->addMenu(QStringLiteral("视图(&W)"));
    view->setObjectName("viewMenu");
    for (auto id : {PlayerActions::Playlist, PlayerActions::PlaylistMode, PlayerActions::Fullscreen}) view->addAction(action(id));
    auto *help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    help->addAction(action(PlayerActions::Shortcuts)); help->addAction(action(PlayerActions::About)); help->addAction(action(PlayerActions::Licenses));
    auto *context = new QMenu(video_);
    context->setObjectName("videoContextMenu");
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::SeekGoto, PlayerActions::Open, PlayerActions::FeatureMode,
                   PlayerActions::StandardMode, PlayerActions::ResetView, PlayerActions::Mute, PlayerActions::Playlist, PlayerActions::Fullscreen}) context->addAction(action(id));
    video_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(video_, &QWidget::customContextMenuRequested, this, [this, context](QPoint point) {
        video_->cancelInput();
        context->popup(video_->mapToGlobal(point.x() < 0 ? video_->rect().center() : point));
    });
    connect(video_, &VideoWidget::fullscreenRequested, action(PlayerActions::Fullscreen), &QAction::trigger);
    connect(video_, &VideoWidget::zoomChanged, this, [this](double scale) {
        controls_->setZoom(scale); surface_->showOsd("zoom", QStringLiteral("画面 %1×").arg(scale, 0, 'f', 2));
    });
    connect(video_, &VideoWidget::rendererReadyChanged, &player_, &Player::setRendererReady);
    connect(video_, &VideoWidget::playbackToggleRequested, this, [this] {
        if (actions_->get(PlayerActions::PlayPause)->isEnabled()) actions_->get(PlayerActions::PlayPause)->trigger();
    });
    connect(video_, &VideoWidget::volumeStepsRequested, this, [this](double steps) { if (audioAvailable_) player_.setVolume(player_.volumeTarget() + 5 * steps); });
    connect(controls_, &ControlBar::seekRequested, this, [this](int seconds) { if (state_ == Player::State::Loaded && seekable_) player_.seek(seconds); });
    connect(controls_, &ControlBar::volumeRequested, &player_, &Player::setVolume);
    connect(&player_, &Player::mediaChanged, this, &MainWindow::updateMedia);
    connect(&player_, &Player::fileOpening, this, [this] { controls_->cancelSeekPreview(); video_->cancelInput(); surface_->dismissOsd(); video_->setVideoSize({}); controls_->setZoom(1); });
    connect(&player_, &Player::durationChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { durationSeconds_ = seconds; duration_ = wholeSeconds(seconds); controls_->setTimeline(position_, duration_, seekable_); updateSeekActions(); }
    });
    connect(&player_, &Player::positionChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { position_ = wholeSeconds(seconds); controls_->setTimeline(position_, duration_, seekable_); }
    });
    connect(&player_, &Player::seekableChanged, this, [this](bool seekable) {
        seekable_ = seekable; controls_->setTimeline(position_, duration_, state_ == Player::State::Loaded && seekable_);
        updateSeekActions();
    });
    connect(&player_, &Player::pausedChanged, this, [this](bool paused) { paused_ = paused; updatePresentation(); });
    connect(&player_, &Player::volumeChanged, this, [this](double volume) {
        controls_->setVolume(volume);
        if (audioAvailable_) surface_->showOsd("audio", QStringLiteral("%1 · 音量 %2%").arg(player_.muted() ? QStringLiteral("静音") : QStringLiteral("有声")).arg(qRound(volume)));
    });
    connect(&player_, &Player::mutedChanged, this, [this](bool muted) {
        actions_->setMuted(muted); controls_->setMuted(muted);
        if (audioAvailable_) surface_->showOsd("audio", muted ? QStringLiteral("已静音") : QStringLiteral("已取消静音"));
    });
    connect(&player_, &Player::audioAvailableChanged, this, [this](bool available) {
        audioAvailable_ = available; actions_->setAudioAvailable(available); controls_->setAudioAvailable(available);
    });
    connect(&player_, &Player::seekRequested, this, [this](double target) { surface_->showOsd("seek", QStringLiteral("定位 %1").arg(PlaybackDialogs::timestamp(target))); });
    connect(&player_, &Player::subtitleLoaded, this, [this](const QString &path) { surface_->showOsd("subtitle", QStringLiteral("已加载字幕：%1").arg(QFileInfo(path).fileName()), 2000); });
    connect(&player_, &Player::videoSizeChanged, this, [this](QSize size) {
        if (state_ == Player::State::Loaded) { videoSize_ = size; video_->setVideoSize(size); updatePresentation(); }
    });
    connect(&player_, &Player::commandError, this, [this](const QString &message) {
        controls_->setStatus(QStringLiteral("操作失败"), {}); controls_->setToolTip(message);
        surface_->showOsd("error", QStringLiteral("操作失败：%1").arg(message), 3000);
    });
    setMinimumSize(640, 400);
    new MediaControls(player_,*actions_,*video_,*surface_,*controls_,*this);
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(QSize(1120, 720).boundedTo(available * .9).expandedTo(minimumSize()));
    actions_->setAudioAvailable(false); controls_->setAudioAvailable(false);
    updateMedia(Player::State::Idle, {}, {});
    video_->setFocus();
}

bool MainWindow::videoContextValid() const { return video_->isValid(); }

void MainWindow::openFile()
{
    openFiles(false);
}
void MainWindow::openFiles(bool append)
{
    auto *owner = QApplication::activeWindow(); if (!owner) owner = this;
    const QStringList paths = QFileDialog::getOpenFileNames(owner, append ? QStringLiteral("添加媒体文件") : QStringLiteral("打开本地媒体"), {},
        QStringLiteral("媒体文件 (*.mkv *.mp4 *.mov *.avi *.webm *.m4v *.ts *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (paths.isEmpty()) return;
    QList<QUrl> urls; for (const auto &path : paths) urls.append(QUrl::fromLocalFile(path)); ingestUrls(urls, !append);
    if (owner == this) video_->setFocus();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) { if (wasMaximized_) showMaximized(); else showNormal(); }
    else { wasMaximized_ = isMaximized(); showFullScreen(); }
    video_->setFocus();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (!actions_) return;
    if (event->type() == QEvent::WindowStateChange) {
        actions_->setFullscreen(isFullScreen()); menuBar()->setVisible(!isFullScreen());
        surface_->setFullscreen(isFullScreen());
        playlistHost_->setFullscreen(isFullScreen());
        controls_->show(); unsetCursor();
        if (isFullScreen()) revealControls(); else hideTimer_.stop();
    }
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) revealControls();
        else { controls_->show(); unsetCursor(); hideTimer_.stop(); video_->cancelInput(); controls_->cancelSeekPreview(); }
    }
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        actions_->refreshIcons(); if (controls_) controls_->refreshIcons();
    }
}

void MainWindow::updateMedia(Player::State state, const QString &path, const QString &error)
{
    state_ = state; path_ = path; error_ = error;
    const QString file = QFileInfo(path).fileName();
    setWindowTitle(file.isEmpty() ? QCoreApplication::applicationName() : file + QStringLiteral(" — ") + QCoreApplication::applicationName());
    if (state != Player::State::Loaded) {
        audioAvailable_ = false; actions_->setAudioAvailable(false); controls_->setAudioAvailable(false);
        controls_->cancelSeekPreview(); video_->cancelInput(); controls_->setZoom(1);
        seekable_ = false; videoSize_ = {};
        if (state != Player::State::Loading) video_->setVideoSize({});
    }
    if (state == Player::State::Loading || state == Player::State::Idle || state == Player::State::Error) {
        position_ = 0; duration_ = 0; durationSeconds_ = 0; paused_ = false;
    } else if (state == Player::State::Ended) position_ = duration_;
    controls_->setTimeline(position_, duration_, state == Player::State::Loaded && seekable_);
    updateSeekActions();
    updatePresentation();
    if (state == Player::State::Error) {
        surface_->showOsd("error", QStringLiteral("无法打开文件：%1").arg(error), 3000); revealControls();
    }
}

void MainWindow::updatePresentation()
{
    const bool loaded = state_ == Player::State::Loaded;
    const bool replayable = !path_.isEmpty() && (state_ == Player::State::Ended || state_ == Player::State::Idle);
    actions_->setMediaState(loaded, paused_, replayable, loaded && !videoSize_.isEmpty(), state_ == Player::State::Error);
    if (!loaded && playlist_->model().rowCount() && state_ == Player::State::Idle) actions_->get(PlayerActions::PlayPause)->setEnabled(true);
    actions_->get(PlayerActions::Stop)->setEnabled(loaded || state_ == Player::State::Loading);
    const QString file = QFileInfo(path_).fileName();
    QString status;
    switch (state_) {
    case Player::State::Idle:
        status = path_.isEmpty() ? QStringLiteral("就绪") : QStringLiteral("已停止");
        surface_->present(path_.isEmpty() ? QStringLiteral("打开视频，开始观看") : QStringLiteral("已停止"),
            path_.isEmpty() ? QStringLiteral("支持常见本地媒体格式\n按 Ctrl+O 选择文件") : file,
            actions_->get(replayable ? PlayerActions::PlayPause : PlayerActions::Open));
        break;
    case Player::State::Loading:
        status = QStringLiteral("正在打开…"); surface_->present(status, file, nullptr); break;
    case Player::State::Error:
        status = QStringLiteral("打开失败"); surface_->present(QStringLiteral("无法打开此文件"), file + '\n' + error_,
            actions_->get(PlayerActions::Retry), actions_->get(PlayerActions::Open)); break;
    case Player::State::Ended:
        status = QStringLiteral("播放完毕"); surface_->present(status, file, actions_->get(PlayerActions::PlayPause), actions_->get(PlayerActions::Open)); break;
    case Player::State::Loaded:
        status = paused_ ? QStringLiteral("已暂停") : QStringLiteral("正在播放");
        if (videoSize_.isEmpty()) surface_->present(QStringLiteral("音频播放"), file, nullptr);
        else surface_->showVideo();
        break;
    }
    controls_->setStatus(status, videoSize_.isEmpty() ? QString() : QStringLiteral("%1 × %2").arg(videoSize_.width()).arg(videoSize_.height()));
}

void MainWindow::showLicenses()
{
    QDialog dialog(QApplication::activeWindow() ? QApplication::activeWindow() : this); dialog.setWindowTitle(QStringLiteral("第三方许可 · Lucide"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit; text->setReadOnly(true); text->setAccessibleName(QStringLiteral("Lucide 完整许可"));
    QFile license(":/licenses/lucide.txt");
    if (license.open(QIODevice::ReadOnly)) text->setPlainText(QString::fromUtf8(license.readAll()));
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(720, 500); dialog.exec();
}

void MainWindow::updateSeekActions()
{
    actions_->setSeekable(state_ == Player::State::Loaded && seekable_ && std::isfinite(durationSeconds_) && durationSeconds_ > 0);
}

void MainWindow::gotoTime()
{
    auto *owner = QApplication::activeWindow() ? QApplication::activeWindow() : this;
    if (const auto time = PlaybackDialogs::gotoTime(owner, player_.positionTarget(), durationSeconds_)) player_.seek(*time);
    if (owner == this) video_->setFocus();
}

void MainWindow::showShortcuts() { PlaybackDialogs::shortcuts(QApplication::activeWindow() ? QApplication::activeWindow() : this, *actions_); }

void MainWindow::revealControls()
{
    if (!isFullScreen()) return;
    controls_->show(); controls_->raise(); unsetCursor(); hideTimer_.start();
}

void MainWindow::hideControls()
{
    if (!isFullScreen()) return;
    auto *focus = QApplication::focusWidget();
    const bool controlFocus = focus && (focus == controls_ || controls_->isAncestorOf(focus));
    const bool pointerInControls = controls_->rect().contains(controls_->mapFromGlobal(QCursor::pos()));
    auto *seek = controls_->findChild<QSlider *>("seekSlider");
    if (!isActiveWindow() || QApplication::activePopupWidget() || QApplication::activeModalWidget()
        || controlFocus || pointerInControls || seek->isSliderDown() || video_->gestureActive() || playlistHost_->overlayVisible() || state_ == Player::State::Error) {
        hideTimer_.start(); return;
    }
    controls_->hide(); setCursor(Qt::BlankCursor);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(object);
    if (widget && (widget == this || isAncestorOf(widget))) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::KeyPress:
            revealControls(); break;
        default: break;
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    event->acceptProposedAction(); revealControls();
    ingestUrls(event->mimeData()->urls(), !event->modifiers().testFlag(Qt::ControlModifier));
}

void MainWindow::setupPlaylist()
{
    playlist_ = new PlaylistController(player_, this); playlist_->setObjectName("playlistController");
    panel_ = new PlaylistPanel(playlist_->model(), *actions_);
    playlistHost_ = new PlaylistHost(*this, *centralWidget(), *splitter_, panel_);
    scanner_ = new FolderScanner(this); scanner_->setObjectName("folderScanner");
    actions_->attachPlaylist(*playlistHost_->window(), *panel_->view());
    const auto action = [this](PlayerActions::Id id) { return actions_->get(id); };
    connect(action(PlayerActions::Add), &QAction::triggered, this, [this] { openFiles(true); });
    connect(action(PlayerActions::AddFolder), &QAction::triggered, this, &MainWindow::addFolder);
    connect(action(PlayerActions::CancelScan), &QAction::triggered, scanner_, &FolderScanner::cancel);
    connect(action(PlayerActions::Previous), &QAction::triggered, playlist_, &PlaylistController::previous);
    connect(action(PlayerActions::Next), &QAction::triggered, playlist_, &PlaylistController::next);
    connect(action(PlayerActions::Playlist), &QAction::triggered, playlistHost_, &PlaylistHost::toggle);
    connect(action(PlayerActions::PlaylistMode), &QAction::triggered, this, [this] { playlistHost_->setDetached(!playlistHost_->detached()); });
    connect(action(PlayerActions::ClosePlaylist), &QAction::triggered, this, [this] { playlistHost_->setVisible(false); });
    connect(action(PlayerActions::QueuePlay), &QAction::triggered, this, [this] { playlist_->play(panel_->selectedId()); });
    connect(action(PlayerActions::QueueRemove), &QAction::triggered, this, [this] { playlist_->remove(panel_->selectedIds()); });
    connect(action(PlayerActions::QueueUp), &QAction::triggered, this, [this] { playlist_->model().moveSelected(panel_->selectedIds(), -1); });
    connect(action(PlayerActions::QueueDown), &QAction::triggered, this, [this] { playlist_->model().moveSelected(panel_->selectedIds(), 1); });
    connect(action(PlayerActions::QueueClear), &QAction::triggered, playlist_, &PlaylistController::clear);
    for (auto [id, order] : {std::pair{PlayerActions::OrderSequential, PlaylistController::Order::Sequential},
            {PlayerActions::OrderSingle, PlaylistController::Order::SingleRepeat}, {PlayerActions::OrderList, PlaylistController::Order::ListRepeat},
            {PlayerActions::OrderRandom, PlaylistController::Order::Random}})
        connect(action(id), &QAction::triggered, this, [this, order] { playlist_->setOrder(order); });
    connect(playlist_, &PlaylistController::changed, this, [this] { updatePlaylistActions(); updatePresentation(); });
    connect(playlist_, &PlaylistController::selectItem, panel_, &PlaylistPanel::select);
    connect(playlist_, &PlaylistController::message, this, [this](const QString &text) { surface_->showOsd("queue", text, 3000); });
    connect(panel_, &PlaylistPanel::selectionChanged, this, &MainWindow::updatePlaylistActions);
    connect(panel_, &PlaylistPanel::playItem, playlist_, &PlaylistController::play);
    connect(panel_, &PlaylistPanel::filesDropped, this, [this](const QList<QUrl> &urls, int row) { ingestUrls(urls, false, row); });
    connect(playlistHost_, &PlaylistHost::changed, this, &MainWindow::updatePlaylistActions);
    connect(scanner_, &FolderScanner::busyChanged, panel_, &PlaylistPanel::setScanning);
    connect(scanner_, &FolderScanner::busyChanged, this, [this](bool busy) { actions_->get(PlayerActions::CancelScan)->setEnabled(busy); });
    connect(scanner_, &FolderScanner::canceled, this, [this] { surface_->showOsd("queue", QStringLiteral("已取消文件夹扫描")); });
    connect(scanner_, &FolderScanner::completed, this, [this](const QStringList &paths, int ignored) {
        if (!paths.isEmpty()) { playlist_->addFiles(paths, false); playlistHost_->setVisible(true, false); }
        surface_->showOsd("queue", QStringLiteral("扫描到 %1 个媒体文件，忽略 %2 个其他或不可读文件。").arg(paths.size()).arg(ignored), 3000);
    });
    action(PlayerActions::CancelScan)->setEnabled(false);
    updatePlaylistActions();
}

void MainWindow::updatePlaylistActions()
{
    if (!panel_ || !playlistHost_) return;
    const auto selected = panel_->selectedIds(); const auto &model = playlist_->model();
    actions_->get(PlayerActions::Previous)->setEnabled(playlist_->canPrevious());
    actions_->get(PlayerActions::Next)->setEnabled(playlist_->canNext());
    for (auto [id, order] : {std::pair{PlayerActions::OrderSequential, PlaylistController::Order::Sequential},
            {PlayerActions::OrderSingle, PlaylistController::Order::SingleRepeat}, {PlayerActions::OrderList, PlaylistController::Order::ListRepeat},
            {PlayerActions::OrderRandom, PlaylistController::Order::Random}}) actions_->get(id)->setChecked(playlist_->order() == order);
    actions_->get(PlayerActions::QueuePlay)->setEnabled(!selected.isEmpty());
    actions_->get(PlayerActions::QueueRemove)->setEnabled(!selected.isEmpty());
    actions_->get(PlayerActions::QueueClear)->setEnabled(model.rowCount() > 0);
    bool up = false, down = false;
    for (auto id : selected) { const int row = model.rowOf(id); up |= row > 0 && !selected.contains(model.idAt(row - 1)); down |= row + 1 < model.rowCount() && !selected.contains(model.idAt(row + 1)); }
    actions_->get(PlayerActions::QueueUp)->setEnabled(up); actions_->get(PlayerActions::QueueDown)->setEnabled(down);
    actions_->get(PlayerActions::Playlist)->setChecked(playlistHost_->visible());
    actions_->get(PlayerActions::PlaylistMode)->setText(playlistHost_->detached() ? QStringLiteral("停靠回主窗口") : QStringLiteral("独立窗口"));
    actions_->refreshIcons();
}

void MainWindow::addFolder()
{
    auto *owner = QApplication::activeWindow() ? QApplication::activeWindow() : this;
    const QString folder = QFileDialog::getExistingDirectory(owner, QStringLiteral("添加文件夹（不含子文件夹）"));
    if (!folder.isEmpty()) { playlistHost_->setVisible(true, false); scanner_->start(folder); }
}

void MainWindow::ingestUrls(const QList<QUrl> &urls, bool replace, int row)
{
    if (urls.isEmpty()) return;
    QStringList media, subtitles; int ignored = 0;
    for (const auto &url : urls) {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) { ++ignored; continue; }
        const auto path = url.toLocalFile();
        if (MediaFiles::subtitle(path)) subtitles.append(path); else media.append(path);
    }
    if (!media.isEmpty()) {
        QStringList matching;
        for (const auto &subtitle : subtitles) {
            if (QFileInfo(subtitle).completeBaseName().compare(QFileInfo(media.first()).completeBaseName(), Qt::CaseInsensitive) == 0) matching.append(subtitle);
            else ++ignored;
        }
        const auto added = playlist_->addFiles(media, replace, row, matching);
        if (added.size() > 1 || !replace) playlistHost_->setVisible(true, false);
        if (QApplication::activeWindow() == this) video_->setFocus();
    } else if (!subtitles.isEmpty()) {
        if (state_ != Player::State::Loaded) surface_->showOsd("drop", QStringLiteral("请先打开媒体，再拖入字幕。"), 3000);
        else for (const auto &subtitle : subtitles) player_.addSubtitle(subtitle);
    }
    if (ignored) surface_->showOsd("drop", QStringLiteral("已忽略 %1 个文件夹、非本地路径或未匹配字幕；字幕可在播放后单独拖入。").arg(ignored), 3000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    scanner_->cancel(); playlistHost_->shutdown(); QMainWindow::closeEvent(event);
}
