#include "controlbar.h"
#include "playbackdialogs.h"
#include "playeractions.h"
#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QPainter>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace { QString timestamp(double seconds); }
class SeekSlider final : public QSlider
{
public:
    SeekSlider(ControlBar &bar, PlayerActions &actions) : QSlider(Qt::Horizontal), bar_(bar), actions_(actions)
    {
        setTracking(false);
        setMouseTracking(true);
        connect(this, &QSlider::valueChanged, this, [this](int value) {
            emit bar_.seekRequested(seconds(value), dragOrigin_.value_or(reported));
        });
    }
    double reported = 0;
    QPointer<QLabel> hover;
    QVariant loopA, loopB;
    QVector<MediaChapter> chapters;
    qsizetype currentChapter = -1;

    double seconds(int position) const { return position * secondsPerTick_; }
    int ticks(double seconds) const { return qRound(std::clamp(seconds / secondsPerTick_, 0.0, double(maximum()))); }
    void setDuration(double duration, bool seekable)
    {
        const double tick = qMax(.001, duration / (std::numeric_limits<int>::max() - 1));
        const int end = qMax(seekable ? 1 : 0, qRound(duration / tick));
        if (maximum() != end || tick != secondsPerTick_ || !seekable) {
            cancel();
            secondsPerTick_ = tick;
            setRange(0, end);
        }
    }

    bool cancel()
    {
        if (!isSliderDown()) return false;
        dragOrigin_.reset();
        pressedChapter_ = -1;
        { const QSignalBlocker block(this); setSliderPosition(value()); setSliderDown(false); setValue(ticks(reported)); }
        emit sliderMoved(value());
        if (hover) hover->hide();
        if (QWidget::mouseGrabber() == this) releaseMouse();
        return true;
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSlider::paintEvent(event);
        if (maximum() <= minimum()) return;
        const bool hasA = loopA.isValid() && loopA.toString() != "no", hasB = loopB.isValid() && loopB.toString() != "no";
        if (!hasA && chapters.isEmpty()) return;
        const auto track = sliderTrack();
        QPainter painter(this);
        if (hasA) {
            auto color = palette().color(QPalette::Highlight);
            const int a = markerX(track, loopA.toDouble()), b = markerX(track, loopB.toDouble());
            if (hasB) { color.setAlpha(65); painter.fillRect(QRect(qMin(a,b),height()/2-6,qMax(1,qAbs(b-a)),12),color); }
            painter.setPen(palette().color(QPalette::Text)); painter.drawLine(a,2,a,height()-2); painter.drawText(a+3,11,"A");
            if (hasB) { painter.drawLine(b,2,b,height()-2); painter.drawText(b+3,11,"B"); }
        }
        painter.setPen(Qt::NoPen);
        for (qsizetype i = 0; i < chapters.size(); ++i) {
            const int x = markerX(track, chapters[i].start), y = height() - 2;
            painter.setBrush(palette().color(i == currentChapter ? QPalette::Highlight : QPalette::ButtonText));
            painter.drawPolygon(QPolygon{{x - 4, y}, {x, y - 7}, {x + 4, y}});
        }
    }
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::ShortcutOverride && hasFocus() && isEnabled()) {
            const auto *key = static_cast<QKeyEvent *>(event);
            const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
            if (modifiers == Qt::NoModifier && (key->key() == Qt::Key_Home || key->key() == Qt::Key_End
                || key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) cancel();
        if (hover && (event->type() == QEvent::Hide || event->type() == QEvent::Leave || event->type() == QEvent::WindowDeactivate)) hover->hide();
        return QSlider::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape && cancel()) { event->accept(); return; }
        const bool canUseKeys = hasFocus() && actions_.viewingInputAllowed();
        if (auto *action = actions_.timelineAction(*event)) {
            if (canUseKeys) {
                cancel();
                actions_.triggerSeekKey(*action, *event);
            }
            event->accept();
            return;
        }
        const int key = event->key();
        if (key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Up || key == Qt::Key_Down) {
            if (canUseKeys) {
                cancel();
                const int step = key == Qt::Key_PageUp ? 10 : key == Qt::Key_PageDown ? -10 : key == Qt::Key_Up ? 1 : -1;
                actions_.seekTimelineStep(step, *event);
            }
            event->accept();
            return;
        }
        if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End) {
            event->accept();
            return;
        }
        QSlider::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) { event->ignore(); return; }
        QStyleOptionSlider option; initStyleOption(&option);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const QPoint point = event->position().toPoint();
        pressedChapter_ = chapterAt(point);
        dragOrigin_ = reported;
        const bool onHandle = pressedChapter_ < 0 && handle.contains(point);
        offset_ = onHandle ? point.x() - handle.left() : handle.width() / 2;
        lastX_ = pressX_ = point.x(); setSliderDown(true);
        if (pressedChapter_ < 0 && !onHandle) previewAt(lastX_);
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const auto point = event->position().toPoint();
        const int x = point.x();
        if (isSliderDown()) moveTo(x);
        if (isEnabled() && hover) {
            const auto index = isSliderDown() ? pressedChapter_ : chapterAt(point);
            QString text;
            if (index >= 0) {
                const auto &chapter = chapters[index];
                text = QStringLiteral("%1  %2").arg(PlaybackDialogs::timestamp(chapter.start), chapter.title);
            } else {
                text = timestamp(seconds(isSliderDown() ? sliderPosition() : at(x, false)));
            }
            auto *host = hover->parentWidget(); const auto position = mapTo(host, QPoint(x, 0));
            hover->setText(hover->fontMetrics().elidedText(text, Qt::ElideRight, qMax(0, host->width() - 2 * hover->margin())));
            hover->adjustSize();
            hover->move(qBound(0, position.x() - hover->width() / 2, qMax(0, host->width() - hover->width())), qMax(0, position.y() - hover->height() - 6));
            hover->show(); hover->raise();
        }
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            moveTo(event->position().toPoint().x());
            const auto chapter = std::exchange(pressedChapter_, -1);
            setSliderDown(false);
            if (chapter >= 0) emit bar_.chapterRequested(chapter, dragOrigin_.value_or(reported));
            dragOrigin_.reset();
            event->accept();
        }
    }
private:
    struct Track { int left, width, handleWidth; bool upsideDown; };
    Track sliderTrack() const
    {
        QStyleOptionSlider option; initStyleOption(&option);
        const auto groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const auto handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        return {groove.left(), qMax(0, groove.width() - handle.width()), handle.width(), option.upsideDown};
    }
    int markerX(const Track &track, double position) const
    {
        return track.left + track.handleWidth / 2
            + QStyle::sliderPositionFromValue(minimum(), maximum(), ticks(position), track.width, track.upsideDown);
    }
    qsizetype chapterAt(QPoint point) const
    {
        if (chapters.isEmpty() || point.y() < height() - 10 || !rect().contains(point)) return -1;
        const auto track = sliderTrack();
        int distance = 6;
        qsizetype nearest = -1;
        for (qsizetype i = 0; i < chapters.size(); ++i) {
            const int delta = qAbs(point.x() - markerX(track, chapters[i].start));
            if (delta < distance) { distance = delta; nearest = i; }
        }
        return nearest;
    }
    void moveTo(int x)
    {
        if (pressedChapter_ >= 0 && qAbs(x - pressX_) < QApplication::startDragDistance()) return;
        pressedChapter_ = -1;
        if (x != lastX_) { lastX_ = x; previewAt(x); }
    }
    void previewAt(int x)
    {
        setSliderPosition(at(x, true));
    }
    int at(int x, bool dragging) const
    {
        const auto track = sliderTrack();
        return QStyle::sliderValueFromPosition(minimum(), maximum(), x - track.left - (dragging ? offset_ : track.handleWidth / 2),
                                               track.width, track.upsideDown);
    }
    ControlBar &bar_;
    PlayerActions &actions_;
    std::optional<double> dragOrigin_;
    double secondsPerTick_ = .001;
    qsizetype pressedChapter_ = -1;
    int offset_ = 0, lastX_ = 0, pressX_ = 0;
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
            const QString tip = QStringLiteral("左键：%1\n右键：%2").arg(
                seek->isEnabled() ? seek->toolTip() : QStringLiteral("当前媒体尚不可定位"), skip->text());
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

QString timestamp(double position)
{
    const int seconds = int(std::clamp(position, 0.0, double(std::numeric_limits<int>::max())));
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
    seek_ = new SeekSlider(*this, actions); seek_->setObjectName("seekSlider");
    seek_->setAccessibleName(QStringLiteral("播放进度"));
    seek_->setRange(0, 0); seek_->setPageStep(10000); seek_->setSingleStep(1000); seek_->setMinimumHeight(24);
    timeline->addWidget(seek_, 1);
    seek_->hover = new QLabel(parent ? parent : this); seek_->hover->setObjectName("seekHoverTime");
    seek_->hover->setAttribute(Qt::WA_TransparentForMouseEvents); seek_->hover->setAutoFillBackground(true); seek_->hover->setTextFormat(Qt::PlainText);
    seek_->hover->setBackgroundRole(QPalette::Button); seek_->hover->setMargin(6); seek_->hover->setFrameShape(QFrame::StyledPanel); seek_->hover->hide();
    connect(seek_, &QSlider::sliderMoved, this, [this](int ticks) { showTime(seek_->seconds(ticks)); });
    connect(&actions_, &PlayerActions::seekStepsChanged, this, &ControlBar::updateSeekDescription);
    connect(&actions_, &PlayerActions::bindingsChanged, this, &ControlBar::updateSeekDescription);

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
    rateButton_ = new QToolButton;
    rateButton_->setObjectName("playbackRateButton");
    rateButton_->setAccessibleName(QStringLiteral("倍速"));
    rateButton_->setFocusPolicy(Qt::StrongFocus);
    rateButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    rateButton_->setPopupMode(QToolButton::InstantPopup);
    rateButton_->setMinimumHeight(32);
    row->addWidget(rateButton_);
    row->addWidget(button(actions.get(PlayerActions::ControlWindowToggle), "controlWindowButton"));
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

void ControlBar::setTimeline(double position, double duration, bool seekable)
{
    const QSignalBlocker block(seek_);
    seek_->reported = std::isfinite(position) ? position : 0;
    duration = std::isfinite(duration) ? qMax(0.0, duration) : 0;
    seek_->setDuration(duration, seekable);
    if (seek_->isEnabled() != seekable) {
        seek_->setEnabled(seekable);
        updateSeekDescription();
    }
    if (!seek_->isSliderDown()) seek_->setValue(seek_->ticks(seek_->reported));
    showTime(seek_->isSliderDown() ? seek_->seconds(seek_->sliderPosition()) : seek_->reported);
    duration_->setText(duration > 0 || seekable ? timestamp(duration) : QStringLiteral("--:--"));
}

void ControlBar::updateSeekDescription()
{
    const auto description = seek_->isEnabled() ? actions_.timelineDescription() : QStringLiteral("当前媒体尚不可定位");
    seek_->setToolTip(description);
    seek_->setAccessibleDescription(description);
}

void ControlBar::showTime(double seconds) { current_->setText(timestamp(seconds)); }

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
void ControlBar::setRateMenu(QMenu *menu) { rateButton_->setMenu(menu); }
void ControlBar::setPlaybackRate(double rate, bool available)
{
    rateButton_->setText(QStringLiteral("%1×").arg(rate > 0 ? rate : 1, 0, 'f', 2));
    rateButton_->setToolTip(QStringLiteral("倍速 %1").arg(rateButton_->text()));
    rateButton_->setEnabled(available);
}
void ControlBar::setLoop(const QVariant &a, const QVariant &b) { seek_->loopA = a; seek_->loopB = b; seek_->update(); }
void ControlBar::setChapters(const QVector<MediaChapter> &chapters)
{
    cancelSeekPreview();
    seek_->chapters = chapters;
    setCurrentChapter(-1);
}

void ControlBar::setCurrentChapter(qsizetype chapter)
{
    seek_->currentChapter = chapter;
    seek_->update();
}

bool ControlBar::cancelSeekPreview()
{
    if (!seek_->cancel()) return false;
    showTime(seek_->reported); return true;
}

bool ControlBar::seekPreviewActive() const { return seek_->isSliderDown(); }

void ControlBar::refreshIcons()
{
    play_->setIcon(Theme::icon(actions_.get(PlayerActions::PlayPause)->property("iconName").toString(), palette(), true));
    volumeButton_->setIcon(Theme::icon(muted_ ? "volume-x" : "volume-2", palette()));
}

void ControlBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    volume_->setFixedWidth(qBound(64, width() / 8, 112));
    rateButton_->setVisible(width() >= 460);
}
