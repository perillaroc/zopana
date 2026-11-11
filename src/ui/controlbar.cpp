#include "controlbar.h"
#include "playeractions.h"
#include "theme.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QPointer>
#include <QPainter>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace { QString timestamp(int seconds); }
class SeekSlider final : public QSlider
{
public:
    SeekSlider() : QSlider(Qt::Horizontal) { setTracking(false); setMouseTracking(true); }
    int reported = 0;
    QPointer<QLabel> hover;
    QVariant loopA, loopB;
    bool cancel()
    {
        if (!isSliderDown()) return false;
        { const QSignalBlocker block(this); setSliderPosition(value()); setSliderDown(false); setValue(reported); }
        emit sliderMoved(reported);
        if (hover) hover->hide();
        if (QWidget::mouseGrabber() == this) releaseMouse();
        return true;
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSlider::paintEvent(event); if (maximum() <= minimum()) return;
        const bool hasA = loopA.isValid() && loopA.toString() != "no", hasB = loopB.isValid() && loopB.toString() != "no";
        if (!hasA) return;
        QStyleOptionSlider option; initStyleOption(&option);
        const auto groove = style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderGroove,this);
        const auto handle = style()->subControlRect(QStyle::CC_Slider,&option,QStyle::SC_SliderHandle,this);
        const auto x = [&](double value) { return groove.x()+handle.width()/2+qRound((groove.width()-handle.width())*(value-minimum())/(maximum()-minimum())); };
        QPainter painter(this); auto color = palette().color(QPalette::Highlight);
        const int a = x(loopA.toDouble()), b = x(loopB.toDouble());
        if (hasB) { color.setAlpha(65); painter.fillRect(QRect(a,height()/2-6,qMax(1,b-a),12),color); }
        painter.setPen(palette().color(QPalette::Text)); painter.drawLine(a,2,a,height()-2); painter.drawText(a+3,11,"A");
        if (hasB) { painter.drawLine(b,2,b,height()-2); painter.drawText(b+3,11,"B"); }
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) cancel();
        if (hover && (event->type() == QEvent::Hide || event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate)) hover->hide();
        return QSlider::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && cancel()) { event->accept(); return; }
        QSlider::keyPressEvent(event);
    }
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
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int x = event->position().toPoint().x();
        if (isSliderDown()) moveTo(x);
        if (isEnabled() && hover) {
            hover->setText(timestamp(isSliderDown() ? sliderPosition() : at(x, false))); hover->adjustSize();
            auto *host = hover->parentWidget(); const auto position = mapTo(host, QPoint(x, 0));
            hover->move(qBound(0, position.x() - hover->width() / 2, host->width() - hover->width()), qMax(0, position.y() - hover->height() - 6));
            hover->show(); hover->raise();
        }
    }
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
        setSliderPosition(at(x, true));
    }
    int at(int x, bool dragging)
    {
        QStyleOptionSlider option; initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        return QStyle::sliderValueFromPosition(minimum(), maximum(), x - groove.x() - (dragging ? offset_ : handle.width() / 2),
                                               groove.width() - handle.width(), option.upsideDown);
    }
    int offset_ = 0, lastX_ = 0;
};

namespace {
class SeekButton final : public QToolButton
{
public:
    SeekButton(QAction *seek, QAction *skip)
        : skip_(skip)
    {
        setDefaultAction(seek);
        setContextMenuPolicy(Qt::PreventContextMenu);
        setMinimumSize(32, 32);
        setIconSize({20, 20});
        setFocusPolicy(Qt::StrongFocus);
        const auto sync = [this, seek, skip] {
            const QString tip = QStringLiteral("左键：%1\n右键：%2").arg(seek->text(), skip->text());
            setEnabled(seek->isEnabled() || skip->isEnabled());
            setToolTip(tip);
            setAccessibleName(seek->text());
            setAccessibleDescription(tip);
        };
        connect(seek, &QAction::changed, this, sync);
        connect(skip, &QAction::changed, this, sync);
        sync();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) event->accept();
        else QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            if (rect().contains(event->position().toPoint())) skip_->trigger();
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

private:
    QAction *skip_;
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
    setAttribute(Qt::WA_StyledBackground);
    setBackgroundRole(QPalette::Button); setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 8); layout->setSpacing(4);
    auto *timeline = new QHBoxLayout; timeline->setSpacing(8); layout->addLayout(timeline);
    current_ = new QLabel(timestamp(0)); current_->setObjectName("currentTimeLabel");
    current_->setAccessibleName(QStringLiteral("当前播放时间"));
    duration_ = new QLabel("--:--"); duration_->setObjectName("durationTimeLabel");
    duration_->setAccessibleName(QStringLiteral("总时长"));
    auto font = current_->font(); font.setFamilies({"Cascadia Mono", "Consolas", font.family()});
    current_->setFont(font); duration_->setFont(font);
    seek_ = new SeekSlider; seek_->setObjectName("seekSlider");
    seek_->setAccessibleName(QStringLiteral("播放进度（秒）"));
    seek_->setRange(0, 0); seek_->setSingleStep(1); seek_->setPageStep(10); seek_->setMinimumHeight(24);
    timeline->addWidget(seek_, 1);
    seek_->hover = new QLabel(parent ? parent : this); seek_->hover->setObjectName("seekHoverTime");
    seek_->hover->setAttribute(Qt::WA_TransparentForMouseEvents); seek_->hover->setAutoFillBackground(true);
    seek_->hover->setBackgroundRole(QPalette::Button); seek_->hover->setMargin(6); seek_->hover->setFrameShape(QFrame::StyledPanel); seek_->hover->hide();
    connect(seek_, &QSlider::sliderMoved, this, &ControlBar::showTime);
    connect(seek_, &QSlider::valueChanged, this, &ControlBar::seekRequested);

    volumeButton_ = button(actions.get(PlayerActions::Mute), "volumeButton");
    volumeButton_->setFixedSize(32, 32); timeline->addWidget(volumeButton_);
    connect(actions.get(PlayerActions::Mute), &QAction::changed, this, [this] { setVolume(volumeValue_); });
    volume_ = new QSlider(Qt::Horizontal); volume_->setRange(0, 100); volume_->setValue(100);
    volume_->setFixedWidth(96); volume_->setMinimumHeight(24);
    volume_->setObjectName("volumeSlider"); volume_->setAccessibleName(QStringLiteral("音量"));
    timeline->addWidget(volume_);
    connect(volume_, &QSlider::valueChanged, this, &ControlBar::volumeRequested);

    auto *row = new QHBoxLayout; row->setSpacing(6); layout->addLayout(row);
    play_ = button(actions.get(PlayerActions::PlayPause), "playPauseButton");
    play_->setFixedSize(40, 40); row->addWidget(play_);
    connect(actions.get(PlayerActions::PlayPause), &QAction::changed, this, [this] { refreshIcons(); });
    row->addWidget(button(actions.get(PlayerActions::Stop), "stopButton"));
    auto *back = new SeekButton(actions.get(PlayerActions::BackSmall), actions.get(PlayerActions::Previous));
    auto *forward = new SeekButton(actions.get(PlayerActions::ForwardSmall), actions.get(PlayerActions::Next));
    back->setObjectName("backButton"); forward->setObjectName("forwardButton");
    row->addWidget(back); row->addWidget(forward);
    row->addWidget(button(actions.get(PlayerActions::Open), "openButton"));
    row->addSpacing(4);
    row->addWidget(current_);
    row->addWidget(new QLabel("/"));
    row->addWidget(duration_);
    row->addStretch();
    row->addWidget(button(actions.get(PlayerActions::Playlist), "playlistButton"));
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
    seek_->reported = position;
    if (seek_->maximum() != duration || !seekable) { seek_->cancel(); seek_->setRange(0, duration); }
    seek_->setEnabled(seekable && duration > 0);
    seek_->setAccessibleDescription(seek_->isEnabled() ? QStringLiteral("可定位") : QStringLiteral("当前媒体尚不可定位"));
    if (!seek_->isSliderDown()) seek_->setValue(position);
    showTime(seek_->isSliderDown() ? seek_->sliderPosition() : position);
    duration_->setText(duration > 0 ? timestamp(duration) : QStringLiteral("--:--"));
}

void ControlBar::showTime(int seconds) { current_->setText(timestamp(seconds)); }

void ControlBar::setVolume(double volume)
{
    volumeValue_ = volume;
    const int percent = qBound(0, qRound(volume), 100);
    if (!volume_->isSliderDown()) { const QSignalBlocker block(volume_); volume_->setValue(percent); }
    volume_->setToolTip(QStringLiteral("音量 %1%").arg(percent));
    volumeButton_->setToolTip(audioAvailable_ ? QStringLiteral("%1 · 音量 %2%").arg(muted_ ? QStringLiteral("取消静音") : QStringLiteral("静音")).arg(percent)
        : QStringLiteral("当前媒体没有可用音轨"));
    volumeButton_->setAccessibleDescription(volumeButton_->toolTip());
}

void ControlBar::setMuted(bool muted) { muted_ = muted; setVolume(volumeValue_); refreshIcons(); }
void ControlBar::setAudioAvailable(bool available)
{
    audioAvailable_ = available;
    volume_->setEnabled(available);
    volumeButton_->setEnabled(available);
    setVolume(volumeValue_);
}
void ControlBar::setLoop(const QVariant &a, const QVariant &b) { seek_->loopA = a; seek_->loopB = b; seek_->update(); }
bool ControlBar::cancelSeekPreview()
{
    if (!seek_->cancel()) return false;
    showTime(seek_->reported); return true;
}

void ControlBar::refreshIcons()
{
    play_->setIcon(Theme::icon(actions_.get(PlayerActions::PlayPause)->property("iconName").toString(), palette(), true));
    volumeButton_->setIcon(Theme::icon(muted_ ? "volume-x" : "volume-2", palette()));
}

void ControlBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    volume_->setFixedWidth(qBound(64, width() / 8, 112));
}
