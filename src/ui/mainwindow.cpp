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
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
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
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWindow>
#include <QScopedValueRollback>
#include <QtMath>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
int wholeSeconds(double value)
{
    return std::isfinite(value) ? int(std::clamp(value, 0.0, double(std::numeric_limits<int>::max()))) : 0;
}

class WindowTitle final : public QLabel
{
public:
    WindowTitle() { setContentsMargins(iconSize + 6, 0, 0, 0); }

    void setTitle(const QString &title)
    {
        setToolTip(title);
        setAccessibleName(title);
        updateText();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        QPainter painter(this);
        windowIcon().paint(&painter, 0, (height() - iconSize) / 2, iconSize, iconSize);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    static constexpr int iconSize = 20;
    void updateText() { setText(fontMetrics().elidedText(toolTip(), Qt::ElideMiddle, contentsRect().width())); }
};

void showMultiplier(QLabel *label, const QString &caption, double value)
{
    label->setText(QStringLiteral("%1 %2×").arg(caption).arg(value, 0, 'f', 2));
    label->setVisible(value > 0 && std::abs(value - 1) >= .005);
}
}

MainWindow::MainWindow(Player &player, QWidget *parent) : QMainWindow(parent), player_(player)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowSystemMenuHint);
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
    auto *menu = setupTopBar();
    setupPlaylist();
    setAcceptDrops(true); surface_->setMouseTracking(true);
    hideTimer_.setSingleShot(true); hideTimer_.setInterval(2500);
    connect(&hideTimer_, &QTimer::timeout, this, &MainWindow::hideControls);
    qApp->installEventFilter(this);
    connect(actions_, &PlayerActions::invoked, this, &MainWindow::revealControls);
    connect(theme, &Theme::Controller::applied, this, [this] {
        actions_->refreshIcons(); controls_->refreshIcons(); menuButton_->setIcon(Theme::icon("ellipsis", palette()));
    });

    const auto action = [this](PlayerActions::Id id) { return actions_->get(id); };
    connect(action(PlayerActions::Open), &QAction::triggered, this, &MainWindow::openFile);
    connect(action(PlayerActions::Retry), &QAction::triggered, this, [this] { if (playlist_->model().currentId()) playlist_->replay(); else if (!path_.isEmpty()) player_.openFile(path_); });
    connect(action(PlayerActions::PlayPause), &QAction::triggered, this, [this] {
        if (state_ == Player::State::Idle && !path_.isEmpty()) player_.skipNextResume();
        if (state_ == Player::State::Loaded) player_.togglePaused();
        else if (playlist_->model().currentId()) playlist_->replay();
        else if (playlist_->model().rowCount()) playlist_->play(panel_->selectedId() ? panel_->selectedId() : playlist_->model().idAt(0));
        else if (!path_.isEmpty()) player_.openFile(path_);
    });
    connect(action(PlayerActions::ResetView), &QAction::triggered, video_, &VideoWidget::resetView);
    connect(action(PlayerActions::Stop), &QAction::triggered, &player_, &Player::stop);
    connect(actions_, &PlayerActions::seekRelativeRequested, &player_, &Player::seekRelative);
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
        QWidget *owner = QApplication::activeWindow() ? QApplication::activeWindow() : this;
        QMessageBox dialog(QMessageBox::NoIcon, QStringLiteral("关于 zopana"),
                           QStringLiteral("zopana\n本地视频播放与画面细节观察\nQt Widgets · libmpv\n图标：Lucide（ISC / Feather MIT）"),
                           QMessageBox::Close, owner);
        dialog.setIconPixmap(windowIcon().pixmap(QSize(48, 48), owner->devicePixelRatioF()));
        auto *licenses = dialog.addButton(actions_->get(PlayerActions::Licenses)->text(), QMessageBox::ActionRole);
        licenses->setObjectName("aboutLicenses");
        dialog.exec();
        if (dialog.clickedButton() == licenses) showLicenses();
    });
    connect(action(PlayerActions::Licenses), &QAction::triggered, this, &MainWindow::showLicenses);

    auto *file = menu->addMenu(QStringLiteral("文件(&F)"));
    file->setObjectName("fileMenu");
    file->addAction(action(PlayerActions::Open)); file->addAction(action(PlayerActions::Add)); file->addAction(action(PlayerActions::AddFolder));
    file->addSeparator(); file->addAction(action(PlayerActions::Quit));
    auto *play = menu->addMenu(QStringLiteral("播放(&P)"));
    play->setObjectName("playMenu");
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::Previous, PlayerActions::Next, PlayerActions::BackSmall, PlayerActions::ForwardSmall,
            PlayerActions::BackMedium, PlayerActions::ForwardMedium, PlayerActions::BackLarge, PlayerActions::ForwardLarge,
            PlayerActions::SeekStart, PlayerActions::SeekEnd, PlayerActions::SeekGoto}) play->addAction(action(id));
    actions_->addPlaybackOrderActions(*play);
    play->addMenu(actions_->playlistSortMenu(play));
    auto *picture = menu->addMenu(QStringLiteral("画面(&V)"));
    picture->setObjectName("pictureMenu");
    picture->addAction(action(PlayerActions::FeatureMode)); picture->addAction(action(PlayerActions::StandardMode));
    picture->addAction(action(PlayerActions::InputMode));
    picture->addSeparator(); picture->addAction(action(PlayerActions::ZoomIn)); picture->addAction(action(PlayerActions::ZoomOut)); picture->addAction(action(PlayerActions::ResetView));
    auto *audio = menu->addMenu(QStringLiteral("音频(&A)"));
    audio->setObjectName("audioMenu");
    for (auto id : {PlayerActions::Mute, PlayerActions::VolumeUp, PlayerActions::VolumeDown}) audio->addAction(action(id));
    auto *view = menu->addMenu(QStringLiteral("视图(&W)"));
    view->setObjectName("viewMenu");
    for (auto id : {PlayerActions::Playlist, PlayerActions::PlaylistMode, PlayerActions::Fullscreen}) view->addAction(action(id));
    auto *help = menu->addMenu(QStringLiteral("帮助(&H)"));
    help->addAction(action(PlayerActions::Shortcuts)); help->addAction(action(PlayerActions::About)); help->addAction(action(PlayerActions::Licenses));
    auto *context = new QMenu(video_);
    context->setObjectName("videoContextMenu");
    for (auto id : {PlayerActions::PlayPause, PlayerActions::Stop, PlayerActions::SeekGoto, PlayerActions::Open, PlayerActions::FeatureMode,
                   PlayerActions::StandardMode, PlayerActions::ResetView, PlayerActions::Mute, PlayerActions::Playlist, PlayerActions::Fullscreen}) context->addAction(action(id));
    for (auto *parent : {picture, context}) {
        auto *rotation = parent->addMenu(QStringLiteral("额外旋转（0°）"));
        rotation->setObjectName("videoRotationMenu");
        for (auto id : {PlayerActions::RotateNext, PlayerActions::RotatePrevious, PlayerActions::RotateReset}) rotation->addAction(action(id));
        rotationMenus_.append(rotation);
    }
    for (auto *parent : {view, context}) {
        auto *sizing = parent->addMenu(QStringLiteral("窗口大小"));
        sizing->setObjectName("videoWindowSizeMenu");
        for (auto id : {PlayerActions::Scale50, PlayerActions::Scale100, PlayerActions::Scale150, PlayerActions::Scale200, PlayerActions::ScaleFit})
            sizing->addAction(action(id));
    }
    const auto rotate = [this](int step) {
        player_.setMediaProperty("video-rotate", player_.propertyTarget("video-rotate").toInt() + step);
    };
    connect(action(PlayerActions::RotateNext), &QAction::triggered, this, [rotate] { rotate(90); });
    connect(action(PlayerActions::RotatePrevious), &QAction::triggered, this, [rotate] { rotate(-90); });
    connect(action(PlayerActions::RotateReset), &QAction::triggered, this, [this] { player_.setMediaProperty("video-rotate", 0); });
    for (int i = 0; i < 5; ++i) {
        connect(action(PlayerActions::Id(PlayerActions::Scale50 + i)), &QAction::triggered, this, [this, i] {
            setWindowSizing(i == 4 ? WindowSizePolicy::FitWorkArea : WindowSizePolicy::VideoScale, i == 4 ? lastVideoScale_ : .5 * (i + 1));
            scheduleVideoResize(true);
        });
    }
    videoResizeTimer_.setSingleShot(true);
    connect(&videoResizeTimer_, &QTimer::timeout, this, &MainWindow::resizeForVideo);
    playlistDockExtent_ = playlistHost_->dockExtent();
    connect(playlistHost_, &PlaylistHost::changed, this, [this] {
        const int extent = playlistHost_->dockExtent();
        if (std::exchange(playlistDockExtent_, extent) != extent) scheduleVideoResize();
    });
    video_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(video_, &QWidget::customContextMenuRequested, this, [this, context](QPoint point) {
        video_->cancelInput();
        context->popup(video_->mapToGlobal(point.x() < 0 ? video_->rect().center() : point));
    });
    connect(video_, &VideoWidget::fullscreenRequested, action(PlayerActions::Fullscreen), &QAction::trigger);
    connect(video_, &VideoWidget::zoomChanged, this, [this](double scale) {
        showMultiplier(zoomLabel_, QStringLiteral("缩放"), scale);
        surface_->showOsd("zoom", QStringLiteral("画面 %1×").arg(scale, 0, 'f', 2));
    });
    connect(video_, &VideoWidget::rendererReadyChanged, &player_, &Player::setRendererReady);
    connect(video_, &VideoWidget::playbackToggleRequested, this, [this] {
        if (actions_->get(PlayerActions::PlayPause)->isEnabled()) actions_->get(PlayerActions::PlayPause)->trigger();
    });
    connect(video_, &VideoWidget::volumeStepsRequested, this, [this](double steps) { if (audioAvailable_) player_.setVolume(player_.volumeTarget() + 5 * steps); });
    connect(controls_, &ControlBar::seekRequested, &player_, &Player::seek);
    connect(controls_, &ControlBar::volumeRequested, &player_, &Player::setVolume);
    connect(&player_, &Player::mediaChanged, this, &MainWindow::updateMedia);
    connect(&player_, &Player::fileOpening, this, [this] {
        controls_->cancelSeekPreview(); video_->cancelInput(); surface_->dismissOsd(); zoomLabel_->hide();
    });
    connect(&player_, &Player::detailsChanged, this, [this] {
        showMultiplier(rateLabel_, QStringLiteral("倍速"), state_ == Player::State::Loaded ? player_.mediaProperty("speed").toDouble() : 1);
        if (actions_->get(PlayerActions::SeekGoto)->isEnabled() != player_.canSeek()) updateTimeline();
    });
    connect(&player_, &Player::durationChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { durationSeconds_ = seconds; duration_ = wholeSeconds(seconds); updateTimeline(); }
    });
    connect(&player_, &Player::positionChanged, this, [this](double seconds) {
        if (state_ == Player::State::Loaded) { position_ = wholeSeconds(seconds); controls_->setTimeline(position_, duration_, player_.canSeek()); }
    });
    connect(&player_, &Player::seekableChanged, this, &MainWindow::updateTimeline);
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
        if (actions_->get(PlayerActions::SeekGoto)->isEnabled() != player_.canSeek()) updateTimeline();
    });
    connect(&player_, &Player::seekRequested, this, [this](double target) { surface_->showOsd("seek", QStringLiteral("定位 %1").arg(PlaybackDialogs::timestamp(target))); });
    connect(&player_, &Player::subtitleLoaded, this, [this](const QString &path) { surface_->showOsd("subtitle", QStringLiteral("已加载字幕：%1").arg(QFileInfo(path).fileName()), 2000); });
    connect(&player_, &Player::videoSizeChanged, this, [this](QSize size) {
        if (state_ != Player::State::Loaded) return;
        const bool resized = videoSize_ != size;
        videoSize_ = size;
        video_->setVideoSize(size, player_.videoRotation());
        showMultiplier(zoomLabel_, QStringLiteral("缩放"), size.isEmpty() ? 1 : video_->zoomScale());
        updatePresentation();
        if (resized) scheduleVideoResize();
    });
    connect(&player_, &Player::videoTransformChanged, this, &MainWindow::updateRotation);
    connect(&player_, &Player::commandError, this, [this](const QString &message) {
        surface_->showOsd("error", QStringLiteral("操作失败：%1").arg(message), 3000);
    });
    setMinimumSize(640, 400);
    new MediaControls(player_,*actions_,*video_,*surface_,*controls_,*this);
    const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(QSize(1120, 720).boundedTo(available * .9).expandedTo(minimumSize()));
    actions_->setAudioAvailable(false); controls_->setAudioAvailable(false);
    updateMedia(Player::State::Idle, {}, {});
    updateRotation();
    video_->setFocus();
}

bool MainWindow::videoContextValid() const { return video_->isValid(); }

QMenu *MainWindow::setupTopBar()
{
    topBar_ = new QWidget(centralWidget());
    topBar_->setObjectName("playerTopBar");
    topBar_->setAttribute(Qt::WA_StyledBackground);
    topBar_->setBackgroundRole(QPalette::Button);
    topBar_->setAutoFillBackground(true);
    topBar_->setFixedHeight(38);
    auto *row = new QHBoxLayout(topBar_);
    row->setContentsMargins(8, 3, 8, 3);
    row->setSpacing(4);
    auto makeButton = [row](const QString &name) {
        auto *button = new QToolButton;
        button->setObjectName(name);
        button->setMinimumSize(32, 32);
        button->setFocusPolicy(Qt::StrongFocus);
        row->addWidget(button);
        return button;
    };
    auto actionButton = [this, makeButton](const QString &name, PlayerActions::Id id, Qt::ToolButtonStyle style) {
        auto *button = makeButton(name);
        auto *action = actions_->get(id);
        button->setDefaultAction(action);
        button->setToolButtonStyle(style);
        const auto sync = [button, action] { button->setAccessibleName(action->text()); };
        connect(action, &QAction::changed, button, sync);
        sync();
        return button;
    };

    menuButton_ = makeButton("moreButton");
    menuButton_->setAccessibleName(QStringLiteral("主菜单"));
    menuButton_->setToolTip(menuButton_->accessibleName());
    menuButton_->setIcon(Theme::icon("ellipsis", palette()));
    auto *menu = new QMenu(menuButton_);
    menu->setObjectName("playerMenu");
    menuButton_->setMenu(menu);
    menuButton_->setPopupMode(QToolButton::InstantPopup);

    title_ = new WindowTitle;
    title_->setObjectName("windowTitleLabel");
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(title_, 1);
    actionButton("featureModeButton", PlayerActions::FeatureMode, Qt::ToolButtonTextOnly);
    actionButton("standardModeButton", PlayerActions::StandardMode, Qt::ToolButtonTextOnly);
    actionButton("resetViewButton", PlayerActions::ResetView, Qt::ToolButtonTextBesideIcon);
    zoomLabel_ = new QLabel;
    zoomLabel_->setObjectName("zoomLabel");
    rateLabel_ = new QLabel;
    rateLabel_->setObjectName("rateLabel");
    for (auto *label : {zoomLabel_, rateLabel_}) { row->addWidget(label); label->hide(); }

    actionButton("fullscreenButton", PlayerActions::Fullscreen, Qt::ToolButtonIconOnly)->setFixedWidth(32);
    minimize_ = makeButton("windowMinimizeButton");
    maximize_ = makeButton("windowMaximizeButton");
    close_ = makeButton("windowCloseButton");
    QFont glyphFont(QStringLiteral("Segoe MDL2 Assets"));
    glyphFont.setPixelSize(12);
    for (auto *button : {minimize_, maximize_, close_}) {
        button->setFont(glyphFont);
        button->setFixedWidth(36);
    }
    minimize_->setText(QStringLiteral("\uE921"));
    minimize_->setAccessibleName(QStringLiteral("最小化"));
    minimize_->setToolTip(minimize_->accessibleName());
    close_->setText(QStringLiteral("\uE8BB"));
    close_->setAccessibleName(QStringLiteral("关闭"));
    close_->setToolTip(close_->accessibleName());
    connect(minimize_, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maximize_, &QToolButton::clicked, this, [this] { if (isMaximized()) showNormal(); else showMaximized(); });
    connect(close_, &QToolButton::clicked, this, &QWidget::close);
    updateWindowButtons();
    static_cast<QVBoxLayout *>(centralWidget()->layout())->insertWidget(0, topBar_);
    return menu;
}

void MainWindow::updateWindowButtons()
{
    const bool maximized = isMaximized();
    maximize_->setText(maximized ? QStringLiteral("\uE923") : QStringLiteral("\uE922"));
    maximize_->setAccessibleName(maximized ? QStringLiteral("还原窗口") : QStringLiteral("最大化"));
    maximize_->setToolTip(maximize_->accessibleName());
    minimize_->setVisible(!isFullScreen());
    maximize_->setVisible(!isFullScreen());
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!isFullScreen()) enableResizeFrame();
    watchWindowScreen();
}

void MainWindow::enableResizeFrame()
{
    // Keep native sizing and Snap support while the client paints the entire frame.
    const auto hwnd = reinterpret_cast<HWND>(winId());
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!(style & WS_THICKFRAME)) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_THICKFRAME);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows_generic_MSG") {
        const auto &msg = *static_cast<MSG *>(message);
        if (msg.message == WM_ENTERSIZEMOVE) movingWindow_ = true;
        if (msg.message == WM_EXITSIZEMOVE) {
            movingWindow_ = false;
            if (videoResizeDeferred_) scheduleVideoResize();
        }
        if (msg.message == WM_SIZING && !applyingVideoResize_) setWindowSizing(WindowSizePolicy::LastGeometry, lastVideoScale_);
        if (msg.message == WM_NCLBUTTONDOWN && msg.wParam == HTMAXBUTTON && maximize_ && maximize_->isVisible()) {
            maximizePressed_ = true;
            maximize_->setDown(true);
            SetCapture(msg.hwnd);
            *result = 0;
            return true;
        }
        if (maximizePressed_ && (msg.message == WM_LBUTTONUP || msg.message == WM_CAPTURECHANGED)) {
            const bool clicked = msg.message == WM_LBUTTONUP && maximize_->rect().contains(maximize_->mapFromGlobal(QCursor::pos()));
            maximizePressed_ = false;
            maximize_->setDown(false);
            if (msg.message == WM_LBUTTONUP) {
                ReleaseCapture();
                if (clicked) maximize_->click();
                *result = 0;
                return true;
            }
        }
        if (msg.message == WM_NCCALCSIZE && msg.wParam && IsZoomed(msg.hwnd)
            && (GetWindowLongPtrW(msg.hwnd, GWL_STYLE) & WS_THICKFRAME)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(msg.hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                auto &rect = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg.lParam)->rgrc[0];
                IntersectRect(&rect, &rect, &monitor.rcWork);
                *result = 0;
                return true;
            }
        }
        if (msg.message == WM_NCHITTEST && topBar_ && !isFullScreen()) {
            POINT point{GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
            ScreenToClient(msg.hwnd, &point);
            if (!isMaximized()) {
                RECT client;
                GetClientRect(msg.hwnd, &client);
                const UINT dpi = GetDpiForWindow(msg.hwnd);
                const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const bool left = point.x < border, right = point.x >= client.right - border;
                const bool top = point.y < border, bottom = point.y >= client.bottom - border;
                if (left || right || top || bottom) {
                    if (left) *result = top ? HTTOPLEFT : bottom ? HTBOTTOMLEFT : HTLEFT;
                    else if (right) *result = top ? HTTOPRIGHT : bottom ? HTBOTTOMRIGHT : HTRIGHT;
                    else *result = top ? HTTOP : HTBOTTOM;
                    return true;
                }
            }
            const QPoint local(qFloor(point.x / devicePixelRatioF()), qFloor(point.y / devicePixelRatioF()));
            const QPoint barPoint = topBar_->mapFrom(this, local);
            if (topBar_->rect().contains(barPoint)) {
                auto *target = topBar_->childAt(barPoint);
                if (target == maximize_) { *result = HTMAXBUTTON; return true; }
                if (!qobject_cast<QToolButton *>(target)) { *result = HTCAPTION; return true; }
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

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

QRect MainWindow::windowedGeometry() const
{
    if (isFullScreen() && geometryBeforeFullscreen_.isValid()) return geometryBeforeFullscreen_;
    return isMaximized() || isFullScreen() || isMinimized() ? normalGeometry() : geometry();
}

void MainWindow::updateRotation()
{
    const bool ready = player_.videoTransformReady();
    video_->setVideoTransform(ready ? std::optional{player_.videoTransformVersion()} : std::nullopt);
    if (!ready) return;
    const int degrees = player_.mediaProperty("video-rotate").toInt();
    const auto title = QStringLiteral("额外旋转（%1°）").arg(degrees);
    if (rotationMenus_.first()->title() == title) return;
    surface_->showOsd("rotation", title);
    for (auto *menu : rotationMenus_) menu->setTitle(title);
}

void MainWindow::setWindowSizing(WindowSizePolicy policy, double scale)
{
    if (sizePolicy_ == policy && lastVideoScale_ == scale) return;
    sizePolicy_ = policy;
    lastVideoScale_ = scale;
    if (policy == WindowSizePolicy::LastGeometry) {
        videoResizeTimer_.stop();
        explicitResize_ = false;
        videoResizeDeferred_ = false;
        playlistHost_->clearVideoSizing();
    } else {
        scheduleVideoResize();
    }
    emit windowSizingChanged();
}

void MainWindow::watchWindowScreen()
{
    auto *handle = windowHandle();
    if (!handle) return;
    if (watchedWindow_ != handle) {
        disconnect(screenChanged_);
        watchedWindow_ = handle;
        handle->installEventFilter(this);
        screenChanged_ = connect(handle, &QWindow::screenChanged, this, [this] {
            watchWindowScreen();
            scheduleVideoResize();
        });
    }
    if (sizingScreen_ != handle->screen()) {
        disconnect(workAreaChanged_);
        sizingScreen_ = handle->screen();
        if (sizingScreen_) workAreaChanged_ = connect(sizingScreen_, &QScreen::availableGeometryChanged, this, [this] { scheduleVideoResize(); });
    }
}

void MainWindow::scheduleVideoResize(bool explicitRequest)
{
    if (applyingVideoResize_ || videoSize_.isEmpty() || sizePolicy_ == WindowSizePolicy::LastGeometry) return;
    explicitResize_ |= explicitRequest;
    if (movingWindow_ && !explicitResize_) { videoResizeDeferred_ = true; return; }
    videoResizeTimer_.start(explicitResize_ ? 0 : 40);
}

void MainWindow::resizeForVideo()
{
    const bool requested = std::exchange(explicitResize_, false);
    if (videoSize_.isEmpty() || !isVisible() || sizePolicy_ == WindowSizePolicy::LastGeometry) return;
    if (!requested && (movingWindow_ || isMinimized() || isFullScreen() || isMaximized())) { videoResizeDeferred_ = true; return; }
    videoResizeDeferred_ = false;
    QScopedValueRollback resizing(applyingVideoResize_, true);
    auto *targetScreen = screen();
    if (isFullScreen() || isMaximized() || isMinimized()) {
        showNormal();
        if (targetScreen && screen() != targetScreen) move(targetScreen->availableGeometry().center() - rect().center());
    }
    centralWidget()->layout()->activate();
    const auto area = screen()->availableGeometry();
    const double dpr = windowHandle()->devicePixelRatio();
    const bool fit = sizePolicy_ == WindowSizePolicy::FitWorkArea;
    const auto solved = solveVideoWindow(videoSize_, dpr, fit ? 0 : lastVideoScale_, area, frameGeometry(),
        windowHandle()->frameMargins(), minimumSize(), topBar_->height() + controls_->height(),
        playlistHost_->dockExtent(), playlistHost_->dockThreshold());
    playlistHost_->beginVideoSizing(solved.docked);
    setGeometry(solved.client);
    centralWidget()->layout()->activate();
    playlistHost_->endVideoSizing();
    const auto viewport = video_->size();
    const double actual = dpr * qMin(double(viewport.width()) / videoSize_.width(), double(viewport.height()) / videoSize_.height());
    const bool limited = !fit && std::abs(actual - lastVideoScale_) * qMax(videoSize_.width(), videoSize_.height()) > 2;
    if (limited) {
        surface_->showOsd("window-size", QStringLiteral("请求 %1%，受窗口或工作区限制，实际 %2%")
            .arg(qRound(lastVideoScale_ * 100)).arg(qRound(actual * 100)));
    } else if (requested) {
        surface_->showOsd("window-size", fit ? QStringLiteral("窗口已适合工作区") : QStringLiteral("窗口 %1%").arg(qRound(lastVideoScale_ * 100)));
    }
    if (requested) video_->setFocus();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        if (wasMaximized_) showMaximized();
        else showNormal();
    } else {
        // On Windows, Qt can report a different normal rectangle after the native frame changes.
        geometryBeforeFullscreen_ = windowedGeometry();
        wasMaximized_ = isMaximized();
        showFullScreen();
    }
    video_->setFocus();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (!actions_) return;
    if (event->type() == QEvent::WindowStateChange) {
        const bool fullscreen = isFullScreen();
        if (!fullscreen && isVisible() && !isMinimized()) enableResizeFrame();
        actions_->setFullscreen(fullscreen);
        auto *layout = static_cast<QVBoxLayout *>(centralWidget()->layout());
        if (fullscreen && layout->indexOf(topBar_) >= 0) {
            layout->removeWidget(topBar_);
            topBar_->setGeometry(0, 0, centralWidget()->width(), topBar_->height());
        } else if (!fullscreen && layout->indexOf(topBar_) < 0) layout->insertWidget(0, topBar_);
        updateWindowButtons();
        surface_->setFullscreen(fullscreen, fullscreen ? topBar_->height() : 0);
        playlistHost_->setFullscreen(fullscreen);
        topBar_->show(); controls_->show(); unsetCursor();
        if (fullscreen) revealControls(); else hideTimer_.stop();
        if (!fullscreen && !isMaximized() && !isMinimized() && videoResizeDeferred_)
            scheduleVideoResize();
    }
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) revealControls();
        else { topBar_->show(); controls_->show(); unsetCursor(); hideTimer_.stop(); video_->cancelInput(); controls_->cancelSeekPreview(); }
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
    static_cast<WindowTitle *>(title_)->setTitle(file.isEmpty() ? QCoreApplication::applicationName() : file);
    showMultiplier(rateLabel_, QStringLiteral("倍速"), state == Player::State::Loaded ? player_.mediaProperty("speed").toDouble() : 1);
    if (state != Player::State::Loaded) {
        audioAvailable_ = false; actions_->setAudioAvailable(false); controls_->setAudioAvailable(false);
        controls_->cancelSeekPreview(); video_->cancelInput(); zoomLabel_->hide();
        videoSize_ = {};
        video_->setVideoTransform(std::nullopt);
        video_->setVideoSize({});
    }
    if (state == Player::State::Loading || state == Player::State::Idle || state == Player::State::Error) {
        position_ = 0; duration_ = 0; durationSeconds_ = 0; paused_ = false;
    } else if (state == Player::State::Ended) position_ = duration_;
    updateTimeline();
    updatePresentation();
    if (state == Player::State::Error) {
        if (!player_.automaticOpen()) surface_->showOsd("error", QStringLiteral("无法打开文件：%1").arg(error), 3000);
        revealControls();
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
    switch (state_) {
    case Player::State::Idle:
        surface_->present(path_.isEmpty() ? QStringLiteral("打开视频，开始观看") : QStringLiteral("已停止"),
            path_.isEmpty() ? QStringLiteral("支持常见本地媒体格式\n按 Ctrl+O 选择文件") : file,
            actions_->get(replayable ? PlayerActions::PlayPause : PlayerActions::Open));
        break;
    case Player::State::Loading:
        surface_->present(QStringLiteral("正在打开…"), file, nullptr); break;
    case Player::State::Error:
        surface_->present(QStringLiteral("无法打开此文件"), file + '\n' + error_,
            actions_->get(PlayerActions::Retry), actions_->get(PlayerActions::Open)); break;
    case Player::State::Ended:
        surface_->present(QStringLiteral("播放完毕"), file, actions_->get(PlayerActions::PlayPause), actions_->get(PlayerActions::Open)); break;
    case Player::State::Loaded:
        if (videoSize_.isEmpty()) surface_->present(QStringLiteral("音频播放"), file, nullptr);
        else surface_->showVideo();
        break;
    }
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

void MainWindow::updateTimeline()
{
    const bool enabled = player_.canSeek();
    actions_->setSeekable(enabled);
    controls_->setTimeline(position_, duration_, enabled);
}

void MainWindow::gotoTime()
{
    auto *owner = QApplication::activeWindow() ? QApplication::activeWindow() : this;
    const auto generation = player_.generation();
    const auto epoch = player_.renderEpoch();
    if (const auto time = PlaybackDialogs::gotoTime(owner, player_.positionTarget(), durationSeconds_);
        time && generation == player_.generation() && epoch == player_.renderEpoch()) player_.seek(*time);
    if (owner == this) video_->setFocus();
}

void MainWindow::showShortcuts() { PlaybackDialogs::shortcuts(QApplication::activeWindow() ? QApplication::activeWindow() : this, *actions_); }

void MainWindow::revealControls()
{
    if (!isFullScreen()) return;
    topBar_->show(); topBar_->raise(); controls_->show(); controls_->raise(); unsetCursor(); hideTimer_.start();
}

void MainWindow::hideControls()
{
    if (!isFullScreen()) return;
    auto *focus = QApplication::focusWidget();
    const bool controlFocus = focus && (controls_->isAncestorOf(focus) || topBar_->isAncestorOf(focus));
    const bool pointerInControls = (controls_->isVisible() && controls_->rect().contains(controls_->mapFromGlobal(QCursor::pos())))
        || (topBar_->isVisible() && topBar_->rect().contains(topBar_->mapFromGlobal(QCursor::pos())));
    auto *seek = controls_->findChild<QSlider *>("seekSlider");
    if (!isActiveWindow() || QApplication::activePopupWidget() || QApplication::activeModalWidget()
        || controlFocus || pointerInControls || seek->isSliderDown() || video_->gestureActive() || playlistHost_->overlayVisible() || surface_->property("resumePending").toBool() || state_ == Player::State::Error) {
        hideTimer_.start(); return;
    }
    topBar_->hide(); controls_->hide(); setCursor(Qt::BlankCursor);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == watchedWindow_ && event->type() == QEvent::DevicePixelRatioChange) scheduleVideoResize();
    auto *widget = qobject_cast<QWidget *>(object);
    if (topBar_ && isFullScreen() && widget && widget->window() == this) {
        switch (event->type()) {
        case QEvent::Resize:
            if (widget == centralWidget()) topBar_->resize(centralWidget()->width(), topBar_->height());
            break;
        case QEvent::MouseMove: {
            const int y = centralWidget()->mapFromGlobal(static_cast<QMouseEvent *>(event)->globalPosition().toPoint()).y();
            if (y >= 0 && y < topBar_->height()) { topBar_->show(); topBar_->raise(); }
            if (y >= centralWidget()->height() - controls_->height()) { controls_->show(); controls_->raise(); }
            unsetCursor(); hideTimer_.start();
            break;
        }
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
    playlistHost_ = new PlaylistHost(*this, *centralWidget(), *splitter_, panel_, *topBar_, *controls_);
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
    for (auto repeatMode : {RepeatMode::None, RepeatMode::One, RepeatMode::All})
        connect(actions_->repeatModeAction(repeatMode), &QAction::triggered, this, [this, repeatMode] {
            playlist_->setPlaybackOrder(repeatMode, playlist_->shuffleEnabled());
        });
    connect(action(PlayerActions::OrderRandom), &QAction::triggered, this, [this] {
        playlist_->setPlaybackOrder(RepeatMode::All, true);
    });
    connect(action(PlayerActions::RepeatCycle), &QAction::triggered, playlist_, &PlaylistController::cycleRepeatMode);
    connect(action(PlayerActions::ShuffleToggle), &QAction::triggered, this, [this](bool checked) {
        playlist_->setPlaybackOrder(playlist_->repeatMode(), checked);
    });
    connect(actions_, &PlayerActions::playlistSortRequested, playlist_, &PlaylistController::sortItems);
    connect(action(PlayerActions::SortAgain), &QAction::triggered, this, [this] { playlist_->sortItems(playlist_->lastSort()); });
    connect(action(PlayerActions::ReverseQueue), &QAction::triggered, playlist_, &PlaylistController::reverseItems);
    connect(action(PlayerActions::ShuffleQueue), &QAction::triggered, playlist_, &PlaylistController::shuffleItems);
    connect(action(PlayerActions::CancelSort), &QAction::triggered, playlist_, &PlaylistController::cancelSort);
    connect(playlist_, &PlaylistController::sortChanged, this, [this] {
        actions_->setPlaylistSortState(playlist_->lastSort(), playlist_->model().rowCount());
        panel_->setSortOrder(playlist_->appliedSort());
    });
    connect(playlist_, &PlaylistController::sortProgress, panel_, &PlaylistPanel::setSortProgress);
    connect(playlist_, &PlaylistController::sortProgress, this, [this](int, int total) {
        actions_->get(PlayerActions::CancelSort)->setEnabled(total > 0);
    });
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
    actions_->setPlaybackOrderState(playlist_->repeatMode(), playlist_->shuffleEnabled());
    actions_->setPlaylistSortState(playlist_->lastSort(), model.rowCount());
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
    scanner_->cancel();
    playlist_->cancelSort();
    playlistHost_->shutdown();
    QMainWindow::closeEvent(event);
}
