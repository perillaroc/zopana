#include "mainwindow.h"
#include "controlbar.h"
#include "playeractions.h"
#include "playbackdialogs.h"
#include "theme.h"
#include "videosurface.h"
#include "videowidget.h"

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
    surface_ = new VideoSurface(video_, central); layout->addWidget(surface_, 1);
    actions_ = new PlayerActions(*this, *video_, *surface_);
    controls_ = new ControlBar(*actions_, surface_); surface_->setControls(controls_);
    setAcceptDrops(true); surface_->setMouseTracking(true);
    hideTimer_.setSingleShot(true); hideTimer_.setInterval(2500);
    connect(&hideTimer_, &QTimer::timeout, this, &MainWindow::hideControls);
    qApp->installEventFilter(this);
    connect(actions_, &PlayerActions::invoked, this, &MainWindow::revealControls);
    connect(theme, &Theme::Controller::applied, this, [this] { actions_->refreshIcons(); controls_->refreshIcons(); });

    const auto action = [this](PlayerActions::Id id) { return actions_->get(id); };
    connect(action(PlayerActions::Open), &QAction::triggered, this, &MainWindow::openFile);
    connect(action(PlayerActions::Retry), &QAction::triggered, this, [this] { if (!path_.isEmpty()) player_.openFile(path_); });
    connect(action(PlayerActions::PlayPause), &QAction::triggered, this, [this] {
        if (state_ == Player::State::Loaded) player_.togglePaused();
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
        if (controls_->cancelSeekPreview() || video_->cancelInput()) { revealControls(); return; }
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
    file->addAction(action(PlayerActions::Open)); file->addSeparator(); file->addAction(action(PlayerActions::Quit));
    auto *play = menuBar()->addMenu(QStringLiteral("播放(&P)"));
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::BackSmall, PlayerActions::ForwardSmall,
            PlayerActions::BackMedium, PlayerActions::ForwardMedium, PlayerActions::BackLarge, PlayerActions::ForwardLarge,
            PlayerActions::SeekStart, PlayerActions::SeekEnd, PlayerActions::SeekGoto}) play->addAction(action(id));
    auto *picture = menuBar()->addMenu(QStringLiteral("画面(&V)"));
    picture->addAction(action(PlayerActions::FeatureMode)); picture->addAction(action(PlayerActions::StandardMode));
    picture->addAction(action(PlayerActions::InputMode));
    picture->addSeparator(); picture->addAction(action(PlayerActions::ZoomIn)); picture->addAction(action(PlayerActions::ZoomOut)); picture->addAction(action(PlayerActions::ResetView));
    auto *audio = menuBar()->addMenu(QStringLiteral("音频(&A)"));
    audio->addMenu(controls_->volumeMenu())->setText(QStringLiteral("音量"));
    for (auto id : {PlayerActions::Mute, PlayerActions::VolumeUp, PlayerActions::VolumeDown}) audio->addAction(action(id));
    menuBar()->addMenu(QStringLiteral("视图(&W)"))->addAction(action(PlayerActions::Fullscreen));
    auto *help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    help->addAction(action(PlayerActions::Shortcuts)); help->addAction(action(PlayerActions::About)); help->addAction(action(PlayerActions::Licenses));
    auto *context = new QMenu(video_);
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::SeekGoto, PlayerActions::Open, PlayerActions::FeatureMode,
                   PlayerActions::StandardMode, PlayerActions::ResetView, PlayerActions::Mute, PlayerActions::Fullscreen}) context->addAction(action(id));
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
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(QSize(1120, 720).boundedTo(available * .9).expandedTo(minimumSize()));
    actions_->setAudioAvailable(false); controls_->setAudioAvailable(false);
    updateMedia(Player::State::Idle, {}, {});
    video_->setFocus();
}

bool MainWindow::videoContextValid() const { return video_->isValid(); }

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开本地视频"), {},
        QStringLiteral("媒体文件 (*.mkv *.mp4 *.mov *.avi *.webm *.m4v *.ts *.m4a *.mp3 *.flac *.wav);;所有文件 (*)"));
    if (!path.isEmpty()) { player_.openFile(path); video_->setFocus(); }
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
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("第三方许可 · Lucide"));
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
    if (const auto time = PlaybackDialogs::gotoTime(this, player_.positionTarget(), durationSeconds_)) player_.seek(*time);
    video_->setFocus();
}

void MainWindow::showShortcuts() { PlaybackDialogs::shortcuts(this, *actions_); }

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
        || controlFocus || pointerInControls || seek->isSliderDown() || video_->gestureActive() || state_ == Player::State::Error) {
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
    const auto urls = event->mimeData()->urls();
    event->acceptProposedAction(); revealControls();
    const auto reject = [this](const QString &message) { surface_->showOsd("drop", message, 3000); };
    if (urls.size() != 1) { reject(QStringLiteral("收到 %1 个文件；请一次拖入一个媒体或字幕文件。").arg(urls.size())); return; }
    if (!urls.first().isLocalFile()) { reject(QStringLiteral("请拖入本地文件。")); return; }
    const QFileInfo file(urls.first().toLocalFile());
    if (!file.isFile() || !file.isReadable()) { reject(QStringLiteral("文件不可读取，或拖入的是文件夹。")); return; }
    const auto suffix = file.suffix().toLower();
    if (QStringList{"srt", "ass", "ssa", "vtt"}.contains(suffix)) {
        if (state_ != Player::State::Loaded) reject(QStringLiteral("请先打开媒体，再拖入字幕。"));
        else player_.addSubtitle(file.absoluteFilePath());
        return;
    }
    if (!QStringList{"mkv", "mp4", "mov", "avi", "webm", "m4v", "ts", "m2ts", "mpeg", "mpg", "wmv", "flv", "m4a", "mp3", "flac", "wav", "ogg", "opus", "aac", "wma"}.contains(suffix)) {
        reject(QStringLiteral("不支持此拖放文件格式；可通过“打开文件”尝试读取。")); return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        reject(QStringLiteral("追加播放需要播放列表；松开 Ctrl 后拖入可替换当前文件。")); return;
    }
    player_.openFile(file.absoluteFilePath()); video_->setFocus();
}
