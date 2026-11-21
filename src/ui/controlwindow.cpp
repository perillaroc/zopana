#include "controlwindow.h"
#include "playbackdialogs.h"
#include "player.h"
#include "playeractions.h"
#include "settingsstore.h"
#include "theme.h"
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
#include <QWheelEvent>
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
    button->setMinimumSize(32, 36);
    button->setIconSize({18, 18});
    return button;
}

QToolButton *actionButton(PlayerActions &actions, PlayerActions::Id id, const char *name,
    Qt::ToolButtonStyle style = Qt::ToolButtonIconOnly)
{
    auto *button = toolButton(name);
    auto *action = actions.get(id);
    button->setDefaultAction(action);
    button->setToolButtonStyle(style);
    const auto syncAccessibility = [button, action] {
        button->setAccessibleName(action->text());
        button->setAccessibleDescription(action->toolTip());
    };
    QObject::connect(action, &QAction::changed, button, syncAccessibility);
    syncAccessibility();
    return button;
}

void useMonospace(QWidget &widget)
{
    auto font = widget.font();
    font.setFamilies({"Cascadia Mono", "Consolas", font.family()});
    widget.setFont(font);
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
    connect(&noticeTimer_, &QTimer::timeout, notice_, &QWidget::hide);
    connect(&player_, &Player::notice, this, [this](const QString &message, bool loopPointConfirmation) {
        if (loopPointConfirmation) {
            noticeTimer_.stop();
            notice_->hide();
            return;
        }
        if (!isVisible() || tabs_->currentIndex() != 0) return;
        notice_->setText(message);
        notice_->show();
        noticeTimer_.start(3500);
    });
    connect(toggle_, &QAction::toggled, this, &ControlWindow::syncVisibility);
    main_.installEventFilter(this);
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] {
        if (placed_) QTimer::singleShot(0, this, &ControlWindow::ensureOnScreen);
    });
    const auto clearGesture = [this] { cancelPan(); notice_->hide(); };
    connect(&player_, &Player::fileOpening, this, clearGesture);
    connect(&player_, &Player::stopping, this, clearGesture);
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
        auto *button = actionButton(actions, id, name, Qt::ToolButtonTextBesideIcon);
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
    auto *navigation = iconButton("controlNavigationButton", "list-video", QStringLiteral("章节与撤销"));
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
    navigationRow->addStretch();
    navigationRow->addWidget(actionButton(actions, Id::FrameBackward, "controlFrameBackward"));
    navigationRow->addWidget(actionButton(actions, Id::PlayPause, "controlPlayPause"));
    navigationRow->addWidget(actionButton(actions, Id::FrameForward, "controlFrameForward"));
    seekLayout->addLayout(navigationRow);
    layout->addWidget(seek);

    auto *speed = new QGroupBox(QStringLiteral("倍速"));
    auto *speedRow = new QHBoxLayout(speed);
    auto *rateDown = actionButton(actions, Id::RateLess, "controlRateLess");
    rate_ = actionButton(actions, Id::RateReset, "controlRateReset", Qt::ToolButtonTextOnly);
    auto *rateUp = actionButton(actions, Id::RateMore, "controlRateMore");
    useMonospace(*rate_);
    rateDown->setMinimumWidth(44);
    rate_->setMinimumWidth(76);
    rateUp->setMinimumWidth(44);
    speedRow->addStretch();
    for (auto *button : {rateDown, rate_, rateUp}) speedRow->addWidget(button);
    speedRow->addStretch();
    layout->addWidget(speed);

    auto *loop = new QGroupBox(QStringLiteral("A–B 循环"));
    auto *loopLayout = new QVBoxLayout(loop);
    loopA_ = actionButton(actions, Id::LoopA, "controlSetA", Qt::ToolButtonTextOnly);
    loopB_ = actionButton(actions, Id::LoopB, "controlSetB", Qt::ToolButtonTextOnly);
    auto *loopButtons = new QHBoxLayout;
    for (auto *button : {loopA_, loopB_}) {
        useMonospace(*button);
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(QStringLiteral("A 00:00:00.000")) + 20);
        loopButtons->addWidget(button, 1);
    }
    loopButtons->addWidget(actionButton(actions, Id::LoopClear, "controlClearLoop"));
    loopLayout->addLayout(loopButtons);
    notice_ = new QLabel;
    notice_->setObjectName("controlLoopNotice");
    notice_->setAccessibleName(QStringLiteral("播放操作提示"));
    notice_->setWordWrap(true);
    loopLayout->addWidget(notice_);
    notice_->hide();
    connect(actions.get(Id::LoopClear), &QAction::triggered, notice_, &QWidget::hide);
    layout->addWidget(loop);
    layout->addStretch();
    return page;
}

QWidget *ControlWindow::createFramingPage(PlayerActions &actions)
{
    using Id = PlayerActions::Id;
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *framing = new QGroupBox(QStringLiteral("取景"));
    auto *framingLayout = new QVBoxLayout(framing);
    auto *zoomRow = new QHBoxLayout;
    zoom_ = new QSlider(Qt::Horizontal);
    zoom_->setObjectName("controlZoomSlider");
    zoom_->setAccessibleName(QStringLiteral("缩放倍率（百分比）"));
    zoom_->setRange(100, 800);
    zoom_->setSingleStep(5);
    zoom_->setPageStep(25);
    zoomValue_ = new QLabel;
    zoomValue_->setObjectName("controlZoomValue");
    zoomValue_->setAccessibleName(QStringLiteral("当前缩放倍率"));
    useMonospace(*zoomValue_);
    zoomValue_->setMinimumWidth(zoomValue_->fontMetrics().horizontalAdvance(QStringLiteral("8.00×")));
    zoomValue_->setAlignment(Qt::AlignCenter);
    zoomRow->addWidget(actionButton(actions, Id::ZoomOut, "controlZoomOut"));
    zoomRow->addWidget(zoom_, 1);
    zoomRow->addWidget(actionButton(actions, Id::ZoomIn, "controlZoomIn"));
    zoomRow->addWidget(zoomValue_);
    zoomRow->addWidget(actionButton(actions, Id::ResetView, "controlFit"));
    framingLayout->addLayout(zoomRow);
    connect(zoom_, &QSlider::valueChanged, this, [this](int percent) { video_.setZoomScale(percent / 100.0); });

    auto *panRow = new QHBoxLayout;
    auto *arrows = new QGridLayout;
    arrows->setSpacing(4);
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
    auto *center = iconButton("controlCenter", "crosshair", QStringLiteral("画面居中"));
    connect(center, &QToolButton::clicked, &video_, &VideoWidget::centerView);
    arrows->addWidget(center, 1, 1);
    panRow->addLayout(arrows);
    panPad_ = new QLabel(QStringLiteral("拖动平移 · 滚轮缩放"));
    panPad_->setObjectName("controlPanPad");
    panPad_->setAccessibleName(QStringLiteral("缩放与平移操作区"));
    panPad_->setToolTip(QStringLiteral("按住左键拖动画面，滚动鼠标滚轮调整缩放"));
    panPad_->setAlignment(Qt::AlignCenter);
    panPad_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    panPad_->setMinimumWidth(130);
    panPad_->setMouseTracking(true);
    panPad_->installEventFilter(this);
    fullViewButton_ = actionButton(actions, Id::HoldFullView, "controlFullView", Qt::ToolButtonTextOnly);
    fullViewButton_->installEventFilter(this);
    auto *padLayout = new QVBoxLayout;
    padLayout->addWidget(panPad_, 1);
    padLayout->addWidget(fullViewButton_, 0, Qt::AlignRight);
    panRow->addLayout(padLayout, 1);
    framingLayout->addLayout(panRow);
    layout->addWidget(framing);

    auto *reference = new QGroupBox(QStringLiteral("参考帧"));
    auto *referenceRow = new QHBoxLayout(reference);
    referenceRow->addWidget(actionButton(actions, Id::FrameBackward, "controlReferenceFrameBackward"));
    referenceRow->addWidget(actionButton(actions, Id::FrameForward, "controlReferenceFrameForward"));
    referenceRow->addSpacing(6);
    referenceRow->addWidget(actionButton(actions, Id::PinReferenceFrame, "controlPinReferenceFrame"));
    referenceRow->addWidget(actionButton(actions, Id::ToggleReferenceFrame, "controlToggleReferenceFrame"));
    referenceRow->addWidget(actionButton(actions, Id::ClearReferenceFrame, "controlClearReferenceFrame"));
    referenceRow->addStretch();
    referenceRow->addWidget(actionButton(actions, Id::CopyViewport, "controlCopyViewport"));
    layout->addWidget(reference);
    layout->addStretch();
    return page;
}

QToolButton *ControlWindow::iconButton(const char *name, const char *icon, const QString &description)
{
    auto *button = toolButton(name);
    button->setProperty("iconName", QString::fromLatin1(icon));
    button->setIcon(Theme::icon(QString::fromLatin1(icon), palette()));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAccessibleName(description);
    button->setToolTip(description);
    button->installEventFilter(this);
    return button;
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

void ControlWindow::updatePlayback()
{
    const double speed = player_.mediaProperty("speed").toDouble();
    const auto rate = QStringLiteral("%1×").arg(speed > 0 ? speed : 1, 0, 'f', 2);
    rate_->defaultAction()->setIconText(rate);
    rate_->defaultAction()->setToolTip(QStringLiteral("当前倍速 %1，点击恢复 1.0×").arg(rate));
    const bool loaded = player_.state() == Player::State::Loaded;
    const auto updatePoint = [this, loaded](QToolButton *button, QChar point, const char *property) {
        const auto time = loaded ? loopPoint(player_.mediaProperty(property)) : QStringLiteral("—");
        auto *action = button->defaultAction();
        action->setIconText(QStringLiteral("%1  %2").arg(point).arg(time));
        action->setToolTip(QStringLiteral("%1 · %2").arg(action->text(), time));
    };
    updatePoint(loopA_, 'A', "ab-loop-a");
    updatePoint(loopB_, 'B', "ab-loop-b");
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
    panAvailable_ = false;
    for (size_t i = 0; i < panButtons_.size(); ++i) {
        const bool enabled = viewReady_ && video_.canPanBy(directions[i].delta);
        panButtons_[i]->setEnabled(enabled);
        panAvailable_ |= enabled;
    }
    if (!panAvailable_) dragPosition_.reset();
    panPad_->setCursor(dragPosition_ ? Qt::ClosedHandCursor : panAvailable_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void ControlWindow::cancelPan()
{
    dragPosition_.reset();
    if (panPad_) panPad_->setCursor(panAvailable_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
    for (auto *button : panButtons_) if (button) button->setDown(false);
}

bool ControlWindow::handlePanPad(QEvent *event)
{
    if (event->type() == QEvent::Wheel && viewReady_) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const double steps = wheel->pixelDelta().isNull() ? wheel->angleDelta().y() / 60.0 : wheel->pixelDelta().y() / 120.0;
        if (steps == 0) return false;
        if (!dragPosition_) video_.zoomSteps(steps);
        wheel->accept();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !panAvailable_) return false;
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
    } else if (event->type() == QEvent::PaletteChange) {
        if (auto *button = qobject_cast<QToolButton *>(object)) {
            const auto icon = button->property("iconName").toString();
            if (!icon.isEmpty()) button->setIcon(Theme::icon(icon, button->palette()));
        }
    } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::UngrabMouse) {
        cancelPan();
    }
    return QWidget::eventFilter(object, event);
}
