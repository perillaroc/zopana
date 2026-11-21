#include "controlwindow.h"
#include "playbackdialogs.h"
#include "player.h"
#include "playeractions.h"
#include "settingsstore.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr struct Direction {
    QPointF delta;
    const char *symbol, *label, *name;
    int row, column;
} directions[] = {
    {{0, -1}, "↑", "向上平移", "panUp", 0, 1},
    {{-1, 0}, "←", "向左平移", "panLeft", 1, 0},
    {{1, 0}, "→", "向右平移", "panRight", 1, 2},
    {{0, 1}, "↓", "向下平移", "panDown", 2, 1}
};

QToolButton *toolButton(const char *name)
{
    auto *button = new QToolButton;
    button->setObjectName(name);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setMinimumSize(32, 32);
    return button;
}

QToolButton *actionButton(PlayerActions &actions, PlayerActions::Id id, const char *name)
{
    auto *button = toolButton(name);
    button->setDefaultAction(actions.get(id));
    return button;
}

QLabel *valueLabel(const char *name, const QString &description)
{
    auto *label = new QLabel;
    label->setObjectName(name);
    label->setAccessibleName(description);
    auto font = label->font();
    font.setFamilies({"Cascadia Mono", "Consolas", font.family()});
    label->setFont(font);
    return label;
}

QString loopPoint(const QVariant &value)
{
    bool numeric = false;
    const double seconds = value.toDouble(&numeric);
    return numeric && value.metaType().id() != QMetaType::Bool && std::isfinite(seconds) && seconds >= 0
        ? PlaybackDialogs::timestamp(seconds) : QStringLiteral("—");
}
}

ControlWindow::ControlWindow(QWidget &main, Player &player, PlayerActions &actions, VideoWidget &video, QMenu &chapters)
    : QWidget(&main, Qt::Tool), main_(main), player_(player), video_(video),
      toggle_(actions.get(PlayerActions::ControlWindowToggle))
{
    setObjectName("controlWindow");
    setWindowTitle(QStringLiteral("控制窗口 · zopana"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMinimumWidth(360);
    addAction(toggle_);

    auto *layout = new QVBoxLayout(this);
    auto *header = new QHBoxLayout;
    auto *play = actionButton(actions, PlayerActions::PlayPause, "controlPlayPause");
    play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    current_ = valueLabel("controlCurrentTime", QStringLiteral("当前播放时间"));
    duration_ = valueLabel("controlDuration", QStringLiteral("总时长"));
    header->addWidget(play);
    header->addStretch();
    header->addWidget(current_);
    header->addWidget(new QLabel("/"));
    header->addWidget(duration_);
    layout->addLayout(header);

    tabs_ = new QTabWidget;
    tabs_->setObjectName("controlTabs");
    tabs_->addTab(createPlaybackPage(actions, chapters), QStringLiteral("播放"));
    framingPage_ = createFramingPage(actions);
    tabs_->addTab(framingPage_, QStringLiteral("缩放与平移"));
    connect(tabs_, &QTabWidget::currentChanged, this, [this] {
        if (tabs_->currentWidget() != framingPage_) video_.cancelFullViewPreview();
    });
    layout->addWidget(tabs_);
    tabs_->setFocus();

    noticeTimer_.setSingleShot(true);
    connect(&noticeTimer_, &QTimer::timeout, notice_, &QLabel::clear);
    connect(&player_, &Player::notice, this, [this](const QString &message) {
        if (!isVisible() || !isActiveWindow() || tabs_->currentIndex() != 0) return;
        notice_->setText(message);
        noticeTimer_.start(3500);
    });
    connect(toggle_, &QAction::toggled, this, &ControlWindow::syncVisibility);
    main_.installEventFilter(this);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        if (placed_) QTimer::singleShot(0, this, &ControlWindow::ensureOnScreen);
    });
    const auto clearGesture = [this] { cancelPan(); notice_->clear(); };
    connect(&player_, &Player::fileOpening, this, clearGesture);
    connect(&player_, &Player::stopping, this, clearGesture);
    connect(&player_, &Player::positionChanged, this, &ControlWindow::updateTime);
    connect(&player_, &Player::durationChanged, this, &ControlWindow::updateTime);
    connect(&player_, &Player::detailsChanged, this, &ControlWindow::updatePlayback);
    connect(&player_, &Player::mediaChanged, this, [this] { updatePlayback(); updateView(); });
    connect(&player_, &Player::videoTransformChanged, this, &ControlWindow::updateView);
    connect(&video_, &VideoWidget::viewChanged, this, &ControlWindow::updateView);
    connect(&video_, &QOpenGLWidget::frameSwapped, this, [this] {
        if (viewAvailable() != viewReady_) updateView();
    });
    updatePlayback();
    updateView();
    adjustSize();
}

QWidget *ControlWindow::createPlaybackPage(PlayerActions &actions, QMenu &chapters)
{
    using Id = PlayerActions::Id;
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);

    auto *seek = new QGroupBox(QStringLiteral("定位"));
    auto *seekLayout = new QVBoxLayout(seek);
    auto *seekRow = new QHBoxLayout;
    seekLayout->addLayout(seekRow);
    const auto seekButton = [&](Id id, const char *name, std::optional<Id> medium = {}) {
        auto *button = actionButton(actions, id, name);
        if (medium) {
            auto *menu = new QMenu(button);
            menu->addAction(actions.get(*medium));
            button->setMenu(menu);
            button->setPopupMode(QToolButton::MenuButtonPopup);
        }
        seekRow->addWidget(button, 1);
    };
    seekButton(Id::BackLarge, "controlBackLarge");
    seekButton(Id::BackSmall, "controlBackSmall", Id::BackMedium);
    seekButton(Id::ForwardSmall, "controlForwardSmall", Id::ForwardMedium);
    seekButton(Id::ForwardLarge, "controlForwardLarge");
    auto *navigation = toolButton("controlNavigationButton");
    navigation->setText(QStringLiteral("章节与撤销"));
    auto *menu = new QMenu(navigation);
    menu->setObjectName("controlNavigationMenu");
    menu->addAction(actions.get(Id::PreviousChapter));
    menu->addAction(actions.get(Id::NextChapter));
    menu->addMenu(&chapters);
    menu->addSeparator();
    menu->addAction(actions.get(Id::UndoSeek));
    navigation->setMenu(menu);
    navigation->setPopupMode(QToolButton::InstantPopup);
    auto *navigationRow = new QHBoxLayout;
    navigationRow->addWidget(navigation);
    navigationRow->addWidget(actionButton(actions, Id::FrameBackward, "controlFrameBackward"));
    navigationRow->addStretch();
    seekLayout->addLayout(navigationRow);
    layout->addWidget(seek);

    auto *speed = new QGroupBox(QStringLiteral("倍速"));
    auto *speedRow = new QHBoxLayout(speed);
    rate_ = valueLabel("controlSpeed", QStringLiteral("当前倍速"));
    speedRow->addWidget(actionButton(actions, Id::RateLess, "controlRateLess"));
    speedRow->addWidget(rate_);
    speedRow->addWidget(actionButton(actions, Id::RateMore, "controlRateMore"));
    speedRow->addStretch();
    speedRow->addWidget(actionButton(actions, Id::RateReset, "controlRateReset"));
    layout->addWidget(speed);

    auto *loop = new QGroupBox(QStringLiteral("A–B 循环"));
    auto *loopLayout = new QVBoxLayout(loop);
    auto *points = new QGridLayout;
    loopA_ = valueLabel("controlLoopA", QStringLiteral("A 点时间"));
    loopB_ = valueLabel("controlLoopB", QStringLiteral("B 点时间"));
    points->addWidget(new QLabel("A"), 0, 0);
    points->addWidget(loopA_, 0, 1);
    points->addWidget(new QLabel("B"), 0, 2);
    points->addWidget(loopB_, 0, 3);
    points->setColumnStretch(1, 1);
    points->setColumnStretch(3, 1);
    loopLayout->addLayout(points);
    auto *loopButtons = new QHBoxLayout;
    loopButtons->addWidget(actionButton(actions, Id::LoopA, "controlSetA"), 1);
    loopButtons->addWidget(actionButton(actions, Id::LoopB, "controlSetB"), 1);
    loopButtons->addWidget(actionButton(actions, Id::LoopClear, "controlClearLoop"), 1);
    loopLayout->addLayout(loopButtons);
    notice_ = new QLabel;
    notice_->setObjectName("controlLoopNotice");
    notice_->setAccessibleName(QStringLiteral("播放操作提示"));
    notice_->setWordWrap(true);
    notice_->setMinimumHeight(notice_->fontMetrics().lineSpacing());
    loopLayout->addWidget(notice_);
    layout->addWidget(loop);
    layout->addStretch();
    return page;
}

QWidget *ControlWindow::createFramingPage(PlayerActions &actions)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *zoomGroup = new QGroupBox(QStringLiteral("缩放"));
    auto *zoomLayout = new QVBoxLayout(zoomGroup);
    auto *zoomRow = new QHBoxLayout;
    auto *less = toolButton("controlZoomOut");
    less->setText(QStringLiteral("−"));
    less->setAccessibleName(QStringLiteral("缩小画面"));
    auto *more = toolButton("controlZoomIn");
    more->setText(QStringLiteral("＋"));
    more->setAccessibleName(QStringLiteral("放大画面"));
    zoom_ = new QSlider(Qt::Horizontal);
    zoom_->setObjectName("controlZoomSlider");
    zoom_->setAccessibleName(QStringLiteral("缩放倍率（百分比）"));
    zoom_->setRange(100, 800);
    zoom_->setSingleStep(5);
    zoom_->setPageStep(25);
    zoomValue_ = valueLabel("controlZoomValue", QStringLiteral("当前缩放倍率"));
    zoomRow->addWidget(less);
    zoomRow->addWidget(zoom_, 1);
    zoomRow->addWidget(more);
    zoomRow->addWidget(zoomValue_);
    zoomLayout->addLayout(zoomRow);
    connect(less, &QToolButton::clicked, this, [this] { video_.zoomSteps(-1); });
    connect(more, &QToolButton::clicked, this, [this] { video_.zoomSteps(1); });
    connect(zoom_, &QSlider::valueChanged, this, [this](int percent) { video_.setZoomScale(percent / 100.0); });
    layout->addWidget(zoomGroup);

    auto *pan = new QGroupBox(QStringLiteral("平移"));
    auto *panRow = new QHBoxLayout(pan);
    auto *arrows = new QGridLayout;
    arrows->setSpacing(2);
    for (size_t i = 0; i < panButtons_.size(); ++i) {
        const auto &direction = directions[i];
        auto *button = panButtons_[i] = toolButton(direction.name);
        button->setText(QString::fromUtf8(direction.symbol));
        button->setAccessibleName(QString::fromUtf8(direction.label));
        button->setToolTip(button->accessibleName());
        button->setAutoRepeat(true);
        button->setAutoRepeatDelay(250);
        button->setAutoRepeatInterval(25);
        button->installEventFilter(this);
        arrows->addWidget(button, direction.row, direction.column);
        connect(button, &QToolButton::clicked, this, [this, delta = direction.delta] { video_.panBy(delta * 12); });
    }
    panRow->addLayout(arrows);
    panPad_ = new QLabel(QStringLiteral("按住左键拖动"));
    panPad_->setObjectName("controlPanPad");
    panPad_->setAccessibleName(QStringLiteral("平移拖动区"));
    panPad_->setAlignment(Qt::AlignCenter);
    panPad_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    panPad_->setMinimumWidth(130);
    panPad_->setMouseTracking(true);
    panPad_->installEventFilter(this);
    panRow->addWidget(panPad_, 1);
    layout->addWidget(pan);

    auto *resetRow = new QHBoxLayout;
    auto *center = toolButton("controlCenter");
    center->setText(QStringLiteral("画面居中"));
    connect(center, &QToolButton::clicked, &video_, &VideoWidget::centerView);
    resetRow->addWidget(center);
    resetRow->addWidget(actionButton(actions, PlayerActions::ResetView, "controlFit"));
    fullViewButton_ = actionButton(actions, PlayerActions::HoldFullView, "controlFullView");
    fullViewButton_->installEventFilter(this);
    resetRow->addWidget(fullViewButton_);
    resetRow->addStretch();
    resetRow->addWidget(actionButton(actions, PlayerActions::CopyViewport, "controlCopyViewport"));
    layout->addLayout(resetRow);

    auto *reference = new QGroupBox(QStringLiteral("参考帧"));
    auto *referenceLayout = new QVBoxLayout(reference);
    auto *referenceRow = new QHBoxLayout;
    for (auto *button : {actionButton(actions, PlayerActions::PinReferenceFrame, "controlPinReferenceFrame"),
            actionButton(actions, PlayerActions::ToggleReferenceFrame, "controlToggleReferenceFrame"),
            actionButton(actions, PlayerActions::ClearReferenceFrame, "controlClearReferenceFrame")}) {
        button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        referenceRow->addWidget(button, 1);
    }
    referenceLayout->addLayout(referenceRow);
    auto *stepRow = new QHBoxLayout;
    stepRow->addWidget(actionButton(actions, PlayerActions::FrameBackward, "controlReferenceFrameBackward"));
    stepRow->addWidget(actionButton(actions, PlayerActions::FrameForward, "controlReferenceFrameForward"));
    stepRow->addStretch();
    referenceLayout->addLayout(stepRow);
    layout->addWidget(reference);
    layout->addStretch();
    return page;
}

void ControlWindow::syncVisibility(bool activate)
{
    const bool requested = toggle_->isChecked();
    if (!requested || !main_.isVisible() || main_.isMinimized()) {
        const bool restoreFocus = !requested && isVisible() && QApplication::activeWindow() == this
            && main_.isVisible() && !main_.isMinimized();
        hide();
        if (restoreFocus) {
            main_.raise();
            main_.activateWindow();
            if (returnFocus_ && returnFocus_->isVisible() && returnFocus_->isEnabled()) returnFocus_->setFocus();
            else video_.setFocus();
        }
        return;
    }
    if (activate) {
        auto *focus = QApplication::focusWidget();
        returnFocus_ = focus && focus->window() == &main_ ? focus : &video_;
    }
    if (!isVisible()) {
        if (!placed_) {
            const QRect work = main_.screen()->availableGeometry();
            const QRect owner = main_.frameGeometry();
            const int right = owner.right() + 12;
            const int x = right + width() <= work.right() ? right : qMin(owner.right(), work.right()) - width() - 16;
            move(x, owner.top() + 32);
            placed_ = true;
        }
        show();
        ensureOnScreen();
    }
    if (activate) {
        raise();
        activateWindow();
    }
}

void ControlWindow::ensureOnScreen()
{
    QList<QRect> areas;
    for (auto *screen : QGuiApplication::screens()) areas.append(screen->availableGeometry());
    const QRect frame = frameGeometry();
    const QSize margins = frame.size() - size();
    const QRect visible = SettingsStore::visibleGeometry(frame, areas, minimumSize().expandedTo(minimumSizeHint()) + margins);
    if (visible.size() != frame.size()) resize(visible.size() - margins);
    if (visible.topLeft() != frame.topLeft()) move(visible.topLeft());
}

void ControlWindow::updateTime()
{
    const bool hasTime = player_.state() == Player::State::Loaded || player_.state() == Player::State::Ended;
    current_->setText(PlaybackDialogs::timestamp(hasTime ? player_.position() : 0));
    duration_->setText(hasTime && player_.duration() > 0 ? PlaybackDialogs::timestamp(player_.duration()) : QStringLiteral("—"));
}

void ControlWindow::updatePlayback()
{
    updateTime();
    const double speed = player_.mediaProperty("speed").toDouble();
    rate_->setText(QStringLiteral("%1×").arg(speed > 0 ? speed : 1, 0, 'f', 2));
    const bool loaded = player_.state() == Player::State::Loaded;
    loopA_->setText(loaded ? loopPoint(player_.mediaProperty("ab-loop-a")) : QStringLiteral("—"));
    loopB_->setText(loaded ? loopPoint(player_.mediaProperty("ab-loop-b")) : QStringLiteral("—"));
}

bool ControlWindow::viewAvailable() const
{
    return player_.videoTransformReady() && video_.canCaptureTransform(player_.videoTransformVersion());
}

void ControlWindow::updateView()
{
    using Input = VideoWidget::FullViewInput;
    viewReady_ = viewAvailable();
    if (!viewReady_) cancelPan();
    framingPage_->setEnabled(viewReady_);
    fullViewButton_->defaultAction()->setEnabled(viewReady_);
    fullViewButton_->setDown(video_.fullViewPreviewActive(Input::ControlMouse) || video_.fullViewPreviewActive(Input::ControlKey));
    const QSignalBlocker blocker(zoom_);
    zoom_->setValue(qRound(video_.zoomScale() * 100));
    zoomValue_->setText(viewReady_ ? QStringLiteral("%1×").arg(video_.zoomScale(), 0, 'f', 2) : QStringLiteral("—"));
    bool canPan = false;
    for (size_t i = 0; i < panButtons_.size(); ++i) {
        const bool enabled = viewReady_ && video_.canPanBy(directions[i].delta);
        panButtons_[i]->setEnabled(enabled);
        canPan |= enabled;
    }
    panPad_->setEnabled(canPan);
    if (!canPan) dragPosition_.reset();
    panPad_->setCursor(dragPosition_ ? Qt::ClosedHandCursor : canPan ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void ControlWindow::cancelPan()
{
    dragPosition_.reset();
    if (panPad_) panPad_->setCursor(panPad_->isEnabled() ? Qt::OpenHandCursor : Qt::ArrowCursor);
    for (auto *button : panButtons_) if (button) button->setDown(false);
}

bool ControlWindow::handlePanPad(QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !panPad_->isEnabled()) return false;
        video_.cancelFullViewPreview();
        dragPosition_ = mouse->position();
        panPad_->setCursor(Qt::ClosedHandCursor);
        return true;
    }
    if (event->type() == QEvent::MouseMove && dragPosition_) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (!mouse->buttons().testFlag(Qt::LeftButton)) {
            cancelPan();
            return true;
        }
        const auto delta = mouse->position() - *dragPosition_;
        dragPosition_ = mouse->position();
        video_.panBy(delta);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && dragPosition_
        && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        const auto delta = static_cast<QMouseEvent *>(event)->position() - *dragPosition_;
        cancelPan();
        video_.panBy(delta);
        return true;
    }
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide) cancelPan();
    return false;
}

bool ControlWindow::handleFullViewButton(QEvent *event)
{
    using Input = VideoWidget::FullViewInput;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
        if (static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) return false;
        if (event->type() == QEvent::MouseButtonRelease) {
            video_.endFullViewPreview(Input::ControlMouse);
        } else if (fullViewButton_->isEnabled()) {
            fullViewButton_->setFocus(Qt::MouseFocusReason);
            video_.beginFullViewPreview(Input::ControlMouse);
        }
        return true;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() != Qt::Key_Space) return false;
        if (key->isAutoRepeat()) return true;
        if (event->type() == QEvent::KeyRelease) video_.endFullViewPreview(Input::ControlKey);
        else if (fullViewButton_->isEnabled()) video_.beginFullViewPreview(Input::ControlKey);
        return true;
    }
    case QEvent::UngrabMouse:
        video_.endFullViewPreview(Input::ControlMouse);
        break;
    case QEvent::FocusOut:
    case QEvent::Hide:
        video_.endFullViewPreview(Input::ControlMouse);
        video_.endFullViewPreview(Input::ControlKey);
        break;
    default:
        break;
    }
    return false;
}

void ControlWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    toggle_->setChecked(false);
}

bool ControlWindow::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide) {
        cancelPan();
        video_.cancelFullViewPreview();
    }
    return QWidget::event(event);
}

bool ControlWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == &main_) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)
            syncVisibility();
    } else if (object == panPad_) {
        if (handlePanPad(event)) return true;
    } else if (object == fullViewButton_) {
        if (handleFullViewButton(event)) return true;
    } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::UngrabMouse) {
        cancelPan();
    }
    return QWidget::eventFilter(object, event);
}
