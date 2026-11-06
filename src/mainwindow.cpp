#include "mainwindow.h"
#include "videowidget.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace {

class SeekSlider final : public QSlider
{
public:
    SeekSlider() : QSlider(Qt::Horizontal) { setTracking(false); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const QPoint point = event->position().toPoint();
        const bool onHandle = handle.contains(point);
        offset_ = onHandle ? point.x() - handle.left() : handle.width() / 2;
        lastX_ = point.x();
        setSliderDown(true);
        if (!onHandle)
            previewAt(lastX_);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown())
            moveTo(event->position().toPoint().x());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->position().toPoint().x());
            setSliderDown(false); // Commit the previewed position exactly once.
            event->accept();
        }
    }

private:
    void moveTo(int x)
    {
        if (x != lastX_) {
            lastX_ = x;
            previewAt(x);
        }
    }

    void previewAt(int x)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(), maximum(), x - groove.x() - offset_,
                                                        groove.width() - handle.width(), option.upsideDown));
    }

    int offset_ = 0;
    int lastX_ = 0;
};

int wholeSeconds(double seconds)
{
    return static_cast<int>(std::clamp(seconds, 0.0, double(std::numeric_limits<int>::max())));
}

QString timestamp(int seconds)
{
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

MainWindow::MainWindow(Player &player, QWidget *parent)
    : QMainWindow(parent), player_(player)
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    video_ = new VideoWidget(player.handle(), this);
    video_->setObjectName(QStringLiteral("videoWidget"));
    video_->setAccessibleName(QStringLiteral("视频画面"));
    layout->addWidget(video_, 1);
    connect(video_, &VideoWidget::rendererReadyChanged, &player_, &Player::setRendererReady);

    auto *controls = new QVBoxLayout;
    controls->setContentsMargins(12, 8, 12, 8);
    layout->addLayout(controls);

    auto *timeline = new QHBoxLayout;
    controls->addLayout(timeline);
    seek_ = new SeekSlider;
    seek_->setObjectName(QStringLiteral("seekSlider"));
    seek_->setAccessibleName(QStringLiteral("播放进度（秒）"));
    seek_->setRange(0, 0);
    seek_->setSingleStep(1);
    seek_->setPageStep(10);
    timeline->addWidget(seek_, 1);
    time_ = new QLabel;
    time_->setObjectName(QStringLiteral("timeLabel"));
    time_->setAccessibleDescription(QStringLiteral("当前时间与总时长"));
    timeline->addWidget(time_);

    auto *actions = new QHBoxLayout;
    controls->addLayout(actions);
    auto *open = new QPushButton(QStringLiteral("打开文件"));
    open->setObjectName(QStringLiteral("openButton"));
    open->setAccessibleName(open->text());
    open->setToolTip(QStringLiteral("打开本地视频 (Ctrl+O)"));
    actions->addWidget(open);
    playPause_ = new QPushButton(QStringLiteral("播放"));
    playPause_->setObjectName(QStringLiteral("playPauseButton"));
    playPause_->setToolTip(QStringLiteral("播放或暂停 (空格)"));
    actions->addWidget(playPause_);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("statusLabel"));
    status_->setAccessibleDescription(QStringLiteral("播放状态"));
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    actions->addWidget(status_, 1);
    videoSize_ = new QLabel;
    videoSize_->setObjectName(QStringLiteral("videoSizeLabel"));
    videoSize_->setAccessibleDescription(QStringLiteral("视频尺寸"));
    actions->addWidget(videoSize_);
    auto *volumeLabel = new QLabel(QStringLiteral("音量 100%"));
    volumeLabel->setObjectName(QStringLiteral("volumeLabel"));
    actions->addWidget(volumeLabel);
    auto *volume = new QSlider(Qt::Horizontal);
    volume->setObjectName(QStringLiteral("volumeSlider"));
    volume->setAccessibleName(QStringLiteral("音量"));
    volume->setRange(0, 100);
    volume->setValue(100);
    volume->setMaximumWidth(110);
    volumeLabel->setBuddy(volume);
    actions->addWidget(volume);
    resetView_ = new QPushButton(QStringLiteral("恢复适应窗口"));
    resetView_->setObjectName(QStringLiteral("resetViewButton"));
    resetView_->setEnabled(false);
    actions->addWidget(resetView_);
    connect(resetView_, &QPushButton::clicked, video_, &VideoWidget::resetView);
    fullscreen_ = new QPushButton(QStringLiteral("全屏"));
    fullscreen_->setObjectName(QStringLiteral("fullscreenButton"));
    fullscreen_->setToolTip(QStringLiteral("切换全屏 (F11)"));
    actions->addWidget(fullscreen_);

    connect(open, &QPushButton::clicked, this, &MainWindow::openFile);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, open, &QPushButton::click);
    connect(playPause_, &QPushButton::clicked, this, [this] { player_.setPaused(!paused_); });
    connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, playPause_, &QPushButton::click);
    connect(fullscreen_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    connect(new QShortcut(QKeySequence(Qt::Key_F11), this), &QShortcut::activated, this, &MainWindow::toggleFullscreen);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] {
        if (isFullScreen())
            toggleFullscreen();
    });

    // Tracking is off: a drag previews sliderMoved, then commits one valueChanged on release.
    connect(seek_, &QSlider::sliderMoved, this, &MainWindow::showTime);
    connect(seek_, &QSlider::valueChanged, this, [this](int seconds) {
        if (loaded_)
            player_.seek(seconds);
    });
    connect(volume, &QSlider::valueChanged, &player_, &Player::setVolume);

    connect(&player_, &Player::mediaChanged, this, &MainWindow::updateMedia);
    connect(&player_, &Player::fileOpening, this, [this] { video_->setVideoSize({}); });
    connect(&player_, &Player::durationChanged, this, [this](double seconds) {
        if (!loaded_)
            return;
        const QSignalBlocker block(seek_);
        seek_->setMaximum(wholeSeconds(seconds));
        seek_->setEnabled(seek_->maximum() > 0);
        showTime(seek_->sliderPosition());
    });
    connect(&player_, &Player::positionChanged, this, [this](double seconds) {
        if (!loaded_ || seek_->isSliderDown())
            return;
        const QSignalBlocker block(seek_);
        seek_->setValue(wholeSeconds(seconds));
        showTime(seek_->value());
    });
    connect(&player_, &Player::pausedChanged, this, [this](bool paused) {
        paused_ = paused;
        if (loaded_) {
            playPause_->setText(paused ? QStringLiteral("播放") : QStringLiteral("暂停"));
            status_->setText(paused ? QStringLiteral("已暂停") : QStringLiteral("正在播放"));
        }
    });
    connect(&player_, &Player::volumeChanged, this, [volume, volumeLabel](double level) {
        const int percent = qBound(0, qRound(level), 100);
        if (!volume->isSliderDown()) {
            const QSignalBlocker block(volume);
            volume->setValue(percent);
        }
        volumeLabel->setText(QStringLiteral("音量 %1%").arg(percent));
    });
    connect(&player_, &Player::videoSizeChanged, this, [this](QSize size) {
        if (loaded_) {
            video_->setVideoSize(size);
            resetView_->setEnabled(!size.isEmpty());
            videoSize_->setText(size.isEmpty() ? QString() :
                QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
        }
    });
    connect(&player_, &Player::commandError, status_, &QLabel::setText);

    updateMedia(Player::State::Idle, {}, {});
    resize(960, 640);
}

bool MainWindow::videoContextValid() const
{
    return video_->isValid();
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开本地视频"), {},
        QStringLiteral("视频文件 (*.mkv *.mp4 *.mov *.avi *.webm *.m4v *.ts);;所有文件 (*)"));
    if (!path.isEmpty())
        player_.openFile(path);
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        if (wasMaximized_)
            showMaximized();
        else
            showNormal();
    } else {
        wasMaximized_ = isMaximized();
        showFullScreen();
    }
    fullscreen_->setText(isFullScreen() ? QStringLiteral("退出全屏") : QStringLiteral("全屏"));
}

void MainWindow::updateMedia(Player::State state, const QString &path, const QString &error)
{
    const QString fileName = QFileInfo(path).fileName();
    setWindowTitle(fileName.isEmpty() ? QStringLiteral("Zopana") : fileName + QStringLiteral(" — Zopana"));
    loaded_ = state == Player::State::Loaded;
    if (!loaded_)
        resetView_->setEnabled(false);
    if (state == Player::State::Idle || state == Player::State::Error || state == Player::State::Ended)
        video_->setVideoSize({});
    playPause_->setEnabled(loaded_);
    playPause_->setText(loaded_ && !paused_ ? QStringLiteral("暂停") : QStringLiteral("播放"));
    seek_->setEnabled(loaded_ && seek_->maximum() > 0);
    const QSignalBlocker block(seek_);
    switch (state) {
    case Player::State::Idle:
    case Player::State::Loading:
    case Player::State::Error:
        seek_->setSliderDown(false);
        seek_->setValue(0);
        seek_->setMaximum(0);
        showTime(0);
        videoSize_->clear();
        if (state == Player::State::Idle)
            status_->clear();
        else if (state == Player::State::Loading)
            status_->setText(QStringLiteral("正在打开…"));
        else
            status_->setText(QStringLiteral("打开失败：%1").arg(error));
        break;
    case Player::State::Loaded:
        status_->setText(paused_ ? QStringLiteral("已暂停") : QStringLiteral("正在播放"));
        break;
    case Player::State::Ended:
        seek_->setValue(seek_->maximum());
        showTime(seek_->value());
        status_->setText(QStringLiteral("播放完毕"));
        break;
    }
}

void MainWindow::showTime(int position)
{
    time_->setText(timestamp(position) + QStringLiteral(" / ") + timestamp(seek_->maximum()));
}
