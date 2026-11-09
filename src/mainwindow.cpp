#include "mainwindow.h"
#include "controlbar.h"
#include "playeractions.h"
#include "theme.h"
#include "videosurface.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
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
    controls_ = new ControlBar(*actions_, central); layout->addWidget(controls_);
    connect(theme, &Theme::Controller::applied, this, [this] { actions_->refreshIcons(); controls_->refreshIcons(); });

    const auto action = [this](PlayerActions::Id id) { return actions_->get(id); };
    connect(action(PlayerActions::Open), &QAction::triggered, this, &MainWindow::openFile);
    connect(action(PlayerActions::Retry), &QAction::triggered, this, [this] { if (!path_.isEmpty()) player_.openFile(path_); });
    connect(action(PlayerActions::PlayPause), &QAction::triggered, this, [this] {
        if (state_ == Player::State::Loaded) player_.togglePaused();
        else if (!path_.isEmpty()) player_.openFile(path_);
    });
    connect(action(PlayerActions::ResetView), &QAction::triggered, video_, &VideoWidget::resetView);
    connect(action(PlayerActions::Fullscreen), &QAction::triggered, this, &MainWindow::toggleFullscreen);
    connect(action(PlayerActions::Escape), &QAction::triggered, this, [this] { if (isFullScreen()) toggleFullscreen(); });
    connect(action(PlayerActions::FeatureMode), &QAction::triggered, video_, [this] { video_->setInputMode(VideoWidget::InputMode::Feature); });
    connect(action(PlayerActions::StandardMode), &QAction::triggered, video_, [this] { video_->setInputMode(VideoWidget::InputMode::Standard); });
    connect(action(PlayerActions::Quit), &QAction::triggered, this, &QWidget::close);
    connect(action(PlayerActions::About), &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("关于 zopana"), QStringLiteral("zopana\n本地视频播放与画面细节观察\nQt Widgets · libmpv\n图标：Lucide（ISC / Feather MIT）\n完整图标许可见“帮助 → 第三方许可”。"));
    });
    connect(action(PlayerActions::Licenses), &QAction::triggered, this, &MainWindow::showLicenses);

    auto *file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    file->addAction(action(PlayerActions::Open)); file->addSeparator(); file->addAction(action(PlayerActions::Quit));
    menuBar()->addMenu(QStringLiteral("播放(&P)"))->addAction(action(PlayerActions::PlayPause));
    auto *picture = menuBar()->addMenu(QStringLiteral("画面(&V)"));
    picture->addAction(action(PlayerActions::FeatureMode)); picture->addAction(action(PlayerActions::StandardMode));
    picture->addSeparator(); picture->addAction(action(PlayerActions::ResetView));
    auto *audio = menuBar()->addMenu(QStringLiteral("音频(&A)"));
    audio->addMenu(controls_->volumeMenu())->setText(QStringLiteral("音量"));
    menuBar()->addMenu(QStringLiteral("视图(&W)"))->addAction(action(PlayerActions::Fullscreen));
    auto *help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    help->addAction(action(PlayerActions::About)); help->addAction(action(PlayerActions::Licenses));
    auto *context = new QMenu(video_);
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Open, PlayerActions::FeatureMode,
                   PlayerActions::StandardMode, PlayerActions::ResetView, PlayerActions::Fullscreen}) context->addAction(action(id));
    video_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(video_, &QWidget::customContextMenuRequested, this, [this, context](QPoint point) {
        context->popup(video_->mapToGlobal(point.x() < 0 ? video_->rect().center() : point));
    });
    connect(video_, &VideoWidget::rendererReadyChanged, &player_, &Player::setRendererReady);
    connect(video_, &VideoWidget::playbackToggleRequested, this, [this] {
        if (actions_->get(PlayerActions::PlayPause)->isEnabled()) actions_->get(PlayerActions::PlayPause)->trigger();
    });
    connect(video_, &VideoWidget::volumeStepsRequested, this, [this](double steps) { player_.setVolume(player_.volumeTarget() + 5 * steps); });
    connect(controls_, &ControlBar::seekRequested, this, [this](int seconds) { if (state_ == Player::State::Loaded && seekable_) player_.seek(seconds); });
    connect(controls_, &ControlBar::volumeRequested, &player_, &Player::setVolume);
    connect(&player_, &Player::mediaChanged, this, &MainWindow::updateMedia);
    connect(&player_, &Player::fileOpening, this, [this] { video_->setVideoSize({}); });
    connect(&player_, &Player::durationChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { duration_ = wholeSeconds(seconds); controls_->setTimeline(position_, duration_, seekable_); }
    });
    connect(&player_, &Player::positionChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { position_ = wholeSeconds(seconds); controls_->setTimeline(position_, duration_, seekable_); }
    });
    connect(&player_, &Player::seekableChanged, this, [this](bool seekable) {
        seekable_ = seekable; controls_->setTimeline(position_, duration_, state_ == Player::State::Loaded && seekable_);
    });
    connect(&player_, &Player::pausedChanged, this, [this](bool paused) { paused_ = paused; updatePresentation(); });
    connect(&player_, &Player::volumeChanged, controls_, &ControlBar::setVolume);
    connect(&player_, &Player::videoSizeChanged, this, [this](QSize size) {
        if (state_ == Player::State::Loaded) { videoSize_ = size; video_->setVideoSize(size); updatePresentation(); }
    });
    connect(&player_, &Player::commandError, this, [this](const QString &message) {
        controls_->setStatus(QStringLiteral("操作失败"), {}); controls_->setToolTip(message);
    });
    setMinimumSize(640, 400);
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(QSize(1120, 720).boundedTo(available * .9).expandedTo(minimumSize()));
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
        seekable_ = false; videoSize_ = {};
        if (state != Player::State::Loading) video_->setVideoSize({});
    }
    if (state == Player::State::Loading || state == Player::State::Idle || state == Player::State::Error) {
        position_ = 0; duration_ = 0; paused_ = false;
    } else if (state == Player::State::Ended) position_ = duration_;
    controls_->setTimeline(position_, duration_, state == Player::State::Loaded && seekable_);
    updatePresentation();
}

void MainWindow::updatePresentation()
{
    const bool loaded = state_ == Player::State::Loaded;
    const bool replayable = !path_.isEmpty() && (state_ == Player::State::Ended || state_ == Player::State::Idle);
    actions_->setMediaState(loaded, paused_, replayable, loaded && !videoSize_.isEmpty(), state_ == Player::State::Error);
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
