#include "controlbar.h"
#include "playeractions.h"
#include "theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {
class SeekSlider final : public QSlider
{
public:
    SeekSlider() : QSlider(Qt::Horizontal) { setTracking(false); }
protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) { event->ignore(); return; }
        QStyleOptionSlider option; initStyleOption(&option);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const QPoint point = event->position().toPoint();
        offset_ = handle.contains(point) ? point.x() - handle.left() : handle.width() / 2;
        lastX_ = point.x(); setSliderDown(true);
        if (!handle.contains(point)) previewAt(lastX_);
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override { if (isSliderDown()) moveTo(event->position().toPoint().x()); }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->position().toPoint().x()); setSliderDown(false); event->accept();
        }
    }
private:
    void moveTo(int x) { if (x != lastX_) { lastX_ = x; previewAt(x); } }
    void previewAt(int x)
    {
        QStyleOptionSlider option; initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        setSliderPosition(QStyle::sliderValueFromPosition(minimum(), maximum(), x - groove.x() - offset_,
                                                        groove.width() - handle.width(), option.upsideDown));
    }
    int offset_ = 0, lastX_ = 0;
};

QString timestamp(int seconds)
{
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
}

ControlBar::ControlBar(PlayerActions &actions, QWidget *parent) : QWidget(parent), actions_(actions)
{
    setObjectName("controlBar");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 6, 16, 10); layout->setSpacing(6);
    auto *timeline = new QHBoxLayout; timeline->setSpacing(12); layout->addLayout(timeline);
    current_ = new QLabel(timestamp(0)); current_->setObjectName("currentTimeLabel");
    current_->setAccessibleName(QStringLiteral("当前播放时间"));
    duration_ = new QLabel("--:--"); duration_->setObjectName("durationTimeLabel");
    duration_->setAccessibleName(QStringLiteral("总时长"));
    auto font = current_->font(); font.setFamilies({"Cascadia Mono", "Consolas", font.family()});
    current_->setFont(font); duration_->setFont(font);
    seek_ = new SeekSlider; seek_->setObjectName("seekSlider");
    seek_->setAccessibleName(QStringLiteral("播放进度（秒）"));
    seek_->setToolTip(QStringLiteral("拖动预览时间，松开定位；聚焦后方向键每次 1 秒"));
    seek_->setRange(0, 0); seek_->setSingleStep(1); seek_->setPageStep(10); seek_->setMinimumHeight(24);
    timeline->addWidget(current_); timeline->addWidget(seek_, 1); timeline->addWidget(duration_);
    connect(seek_, &QSlider::sliderMoved, this, &ControlBar::showTime);
    connect(seek_, &QSlider::valueChanged, this, &ControlBar::seekRequested);

    auto *row = new QHBoxLayout; row->setSpacing(10); layout->addLayout(row);
    open_ = button(actions.get(PlayerActions::Open), "openButton"); row->addWidget(open_);
    play_ = button(actions.get(PlayerActions::PlayPause), "playPauseButton");
    play_->setFixedSize(40, 40); row->addWidget(play_);
    connect(actions.get(PlayerActions::PlayPause), &QAction::changed, this, [this] { refreshIcons(); });
    info_ = new QWidget; auto *infoLayout = new QVBoxLayout(info_);
    infoLayout->setContentsMargins(4, 0, 4, 0); infoLayout->setSpacing(0);
    status_ = new QLabel; status_->setObjectName("statusLabel"); status_->setTextFormat(Qt::PlainText);
    videoSize_ = new QLabel; videoSize_->setObjectName("videoSizeLabel");
    infoLayout->addWidget(status_); infoLayout->addWidget(videoSize_); row->addWidget(info_);
    row->addStretch();
    auto *mode = new QWidget; mode->setObjectName("modeGroup"); auto *modeLayout = new QHBoxLayout(mode);
    modeLayout->setContentsMargins(3, 3, 3, 3); modeLayout->setSpacing(2);
    auto *feature = button(actions.get(PlayerActions::FeatureMode), "featureModeButton", mode);
    auto *standard = button(actions.get(PlayerActions::StandardMode), "standardModeButton", mode);
    for (auto *toggle : {feature, standard}) { toggle->setToolButtonStyle(Qt::ToolButtonTextOnly); modeLayout->addWidget(toggle); }
    row->addWidget(mode);
    reset_ = button(actions.get(PlayerActions::ResetView), "resetViewButton"); row->addWidget(reset_);

    volumeButton_ = new QToolButton; volumeButton_->setObjectName("volumeButton");
    volumeButton_->setAccessibleName(QStringLiteral("音量菜单")); volumeButton_->setFixedSize(32, 32);
    volumeButton_->setFocusPolicy(Qt::StrongFocus);
    volumeMenu_ = new QMenu(this); volumeButton_->setMenu(volumeMenu_); volumeButton_->setPopupMode(QToolButton::InstantPopup);
    auto *volumeAction = new QWidgetAction(volumeMenu_);
    auto *volumePanel = new QWidget; auto *volumeLayout = new QVBoxLayout(volumePanel);
    auto *title = new QLabel(QStringLiteral("音量")); popupVolume_ = new QSlider(Qt::Horizontal);
    popupVolume_->setRange(0, 100); popupVolume_->setValue(100); popupVolume_->setMinimumWidth(160);
    popupVolume_->setAccessibleName(QStringLiteral("弹出音量滑块")); popupVolume_->setObjectName("popupVolumeSlider");
    volumeLayout->addWidget(title); volumeLayout->addWidget(popupVolume_); volumeAction->setDefaultWidget(volumePanel);
    volumeMenu_->addAction(volumeAction); row->addWidget(volumeButton_);
    volume_ = new QSlider(Qt::Horizontal); volume_->setRange(0, 100); volume_->setValue(100);
    volume_->setFixedWidth(100); volume_->setObjectName("volumeSlider"); volume_->setAccessibleName(QStringLiteral("音量"));
    volume_->setToolTip(QStringLiteral("音量 0%–100%")); row->addWidget(volume_);
    volumeLabel_ = new QLabel("100%"); volumeLabel_->setObjectName("volumeLabel"); volumeLabel_->setMinimumWidth(38); row->addWidget(volumeLabel_);
    connect(volume_, &QSlider::valueChanged, this, &ControlBar::volumeRequested);
    connect(popupVolume_, &QSlider::valueChanged, this, &ControlBar::volumeRequested);
    auto *fullscreen = button(actions.get(PlayerActions::Fullscreen), "fullscreenButton"); row->addWidget(fullscreen);
    more_ = new QToolButton; more_->setObjectName("moreButton"); more_->setAccessibleName(QStringLiteral("更多操作"));
    more_->setToolTip(QStringLiteral("更多操作")); more_->setFixedSize(32, 32); more_->setFocusPolicy(Qt::StrongFocus);
    auto *moreMenu = new QMenu(more_);
    for (auto id : {PlayerActions::Open, PlayerActions::FeatureMode, PlayerActions::StandardMode, PlayerActions::ResetView,
                   PlayerActions::Fullscreen, PlayerActions::Quit}) moreMenu->addAction(actions.get(id));
    more_->setMenu(moreMenu); more_->setPopupMode(QToolButton::InstantPopup); row->addWidget(more_);
    setTimeline(0, 0, false); refreshIcons();
}

QToolButton *ControlBar::button(QAction *action, const QString &name, QWidget *parent)
{
    auto *result = new QToolButton(parent ? parent : this);
    result->setDefaultAction(action); result->setObjectName(name); result->setMinimumSize(32, 32);
    result->setIconSize({20, 20}); result->setFocusPolicy(Qt::StrongFocus);
    connect(action, &QAction::changed, result, [result, action] {
        result->setAccessibleName(action->text()); result->setAccessibleDescription(action->toolTip());
    });
    result->setAccessibleName(action->text());
    result->setAccessibleDescription(action->toolTip());
    return result;
}

void ControlBar::setTimeline(int position, int duration, bool seekable)
{
    const QSignalBlocker block(seek_);
    if (seek_->maximum() != duration) { seek_->setSliderDown(false); seek_->setRange(0, duration); }
    seek_->setEnabled(seekable && duration > 0);
    seek_->setAccessibleDescription(seek_->isEnabled() ? QStringLiteral("可定位") : QStringLiteral("当前媒体尚不可定位"));
    if (!seek_->isSliderDown()) seek_->setValue(position);
    showTime(seek_->isSliderDown() ? seek_->sliderPosition() : position);
    duration_->setText(duration > 0 ? timestamp(duration) : QStringLiteral("--:--"));
}

void ControlBar::showTime(int seconds) { current_->setText(timestamp(seconds)); }

void ControlBar::setStatus(const QString &status, const QString &size)
{
    status_->setText(status); videoSize_->setText(size);
}

void ControlBar::setVolume(double volume)
{
    const int percent = qBound(0, qRound(volume), 100);
    for (auto *slider : {volume_, popupVolume_}) {
        if (!slider->isSliderDown()) { const QSignalBlocker block(slider); slider->setValue(percent); }
    }
    volumeLabel_->setText(QStringLiteral("%1%").arg(percent));
    volumeButton_->setToolTip(QStringLiteral("音量 %1%（单击展开）").arg(percent));
}

void ControlBar::refreshIcons()
{
    play_->setIcon(Theme::icon(actions_.get(PlayerActions::PlayPause)->property("iconName").toString(), palette(), true));
    volumeButton_->setIcon(Theme::icon("volume-2", palette()));
    more_->setIcon(Theme::icon("ellipsis", palette()));
}

void ControlBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    info_->setVisible(width() >= 1000);
    volume_->setVisible(width() >= 1000);
    volumeLabel_->setVisible(width() >= 800);
    open_->setVisible(width() >= 800);
    reset_->setToolButtonStyle(width() >= 1000 ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
}
