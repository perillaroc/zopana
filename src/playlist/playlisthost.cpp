#include "playlisthost.h"
#include "playlistpanel.h"
#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QListView>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

class PlaylistWindow final : public QWidget
{
public:
    std::function<void()> hiding;
    PlaylistWindow() : QWidget(nullptr, Qt::Window)
    {
        setObjectName("playlistWindow"); setWindowTitle(QStringLiteral("播放列表 · zopana"));
        setAttribute(Qt::WA_QuitOnClose, false); setMinimumSize(260, 320); resize(320, 560);
        auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    }
protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); if (hiding) hiding(); }
};
class PlaylistScrim final : public QWidget
{
public: using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override { QPainter painter(this); painter.fillRect(rect(), QColor(0, 0, 0, 110)); }
};

PlaylistHost::PlaylistHost(QWidget &main, QWidget &central, QSplitter &splitter, PlaylistPanel *panel)
    : QObject(&main), main_(main), central_(central), splitter_(splitter), panel_(panel), window_(std::make_unique<PlaylistWindow>())
{
    setObjectName("playlistHost");
    dock_ = new QWidget(&splitter); dock_->setObjectName("playlistDock"); dock_->setMinimumWidth(260);
    auto *dockLayout = new QVBoxLayout(dock_); dockLayout->setContentsMargins(0, 0, 0, 0); splitter.addWidget(dock_);
    scrim_ = new PlaylistScrim(&central); scrim_->setObjectName("playlistScrim"); scrim_->installEventFilter(this); scrim_->hide();
    overlay_ = new QWidget(&central); overlay_->setObjectName("playlistOverlay");
    auto *overlayLayout = new QVBoxLayout(overlay_); overlayLayout->setContentsMargins(0, 0, 0, 0); overlay_->hide();
    window_->hiding = [this] { setVisible(false); };
    central.installEventFilter(this);
    connect(&splitter, &QSplitter::splitterMoved, this, [this] {
        if (!dock_->isVisible()) return;
        dockWidth_ = qBound(260, dock_->width(), qMax(260, central_.width() - 520));
        if (dockWidth_ != dock_->width()) splitter_.setSizes({central_.width() - dockWidth_, dockWidth_});
        emit changed();
    });
    arrange();
}
PlaylistHost::~PlaylistHost() = default;
QWidget *PlaylistHost::window() const { return window_.get(); }
void PlaylistHost::restore(bool detached,bool visible,int width) { detached_=detached; visible_=visible; dockWidth_=qBound(260,width,2000); arrange(); emit changed(); }
bool PlaylistHost::overlayVisible() const { return overlay_->isVisible(); }
void PlaylistHost::shutdown() { window_->hide(); if (panel_) panel_->hide(); }
void PlaylistHost::setDetached(bool detached)
{
    if (detached_ == detached) return;
    const bool wasVisible = visible(); detached_ = detached;
    if (detached) visible_ = wasVisible;
    else if (fullscreen_) fullscreenVisible_ = wasVisible;
    arrange(wasVisible); emit changed();
}
void PlaylistHost::setVisible(bool visible, bool activate)
{
    if (visible && !this->visible()) previousFocus_ = QApplication::focusWidget();
    if (fullscreen_ && !detached_) fullscreenVisible_ = visible; else visible_ = visible;
    arrange(visible && activate);
    if (!visible && previousFocus_ && previousFocus_->isVisible() && previousFocus_->window() == &main_) previousFocus_->setFocus();
    emit changed();
}
void PlaylistHost::toggle()
{
    if (detached_ && visible_ && (!window_->isActiveWindow() || window_->isMinimized())) setVisible(true);
    else setVisible(!visible());
}
void PlaylistHost::setFullscreen(bool fullscreen)
{
    if (fullscreen_ == fullscreen) return;
    fullscreen_ = fullscreen; if (fullscreen) fullscreenVisible_ = false;
    arrange(); emit changed();
}
bool PlaylistHost::dismissOverlay() { if (!overlayVisible()) return false; setVisible(false); return true; }
void PlaylistHost::arrange(bool activate)
{
    if (!panel_) return;
    const bool overlay = fullscreen_ || central_.width() < 900;
    QWidget *target = detached_ ? window_.get() : overlay ? overlay_ : dock_;
    const bool moving = panel_->parentWidget() != target;
    const int scroll = panel_->view()->verticalScrollBar()->value();
    if (moving) { panel_->setParent(target); target->layout()->addWidget(panel_); }
    dock_->setVisible(!detached_ && !overlay && visible());
    scrim_->setGeometry(central_.rect()); scrim_->setVisible(!detached_ && overlay && visible());
    overlay_->setGeometry(qMax(0, central_.width() - qMin(320, central_.width() - 48)), 0, qMin(320, central_.width() - 48), central_.height());
    overlay_->setVisible(!detached_ && overlay && visible());
    if (scrim_->isVisible()) { scrim_->raise(); overlay_->raise(); }
    if (detached_ && visible_) {
        if (window_->geometry().intersected(QGuiApplication::primaryScreen()->availableGeometry()).isEmpty()) {
            bool onScreen = false; for (auto *screen : QGuiApplication::screens()) onScreen |= screen->availableGeometry().intersects(window_->geometry());
            if (!onScreen) window_->move(QGuiApplication::primaryScreen()->availableGeometry().center() - QPoint(window_->width() / 2, window_->height() / 2));
        }
        if (activate) { window_->showNormal(); window_->raise(); window_->activateWindow(); }
        else if (!window_->isVisible()) window_->show();
    } else window_->hide();
    panel_->setVisible(visible());
    if (moving) QTimer::singleShot(0, this, [this, scroll] { if (panel_) panel_->view()->verticalScrollBar()->setValue(scroll); });
    if (dock_->isVisible() && moving) splitter_.setSizes({qMax(640, central_.width() - dockWidth_), dockWidth_});
    if (activate) panel_->view()->setFocus();
}
bool PlaylistHost::eventFilter(QObject *object, QEvent *event)
{
    if (object == &central_ && event->type() == QEvent::Resize) arrange();
    if (object == scrim_ && event->type() == QEvent::MouseButtonPress) { setVisible(false); return true; }
    return QObject::eventFilter(object, event);
}
