#pragma once

#include "player.h"
#include "windowsizing.h"
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

class ControlBar;
class PlayerActions;
class VideoSurface;
class VideoWidget;
class PlaylistController;
class PlaylistPanel;
class PlaylistHost;
class FolderScanner;
class QSplitter;
class QLabel;
class QMenu;
class QShowEvent;
class QScreen;
class QToolButton;
class QUrl;
class QWindow;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Player &player, QWidget *parent = nullptr);
    bool videoContextValid() const;
    bool restoreMaximized() const { return isFullScreen()?wasMaximized_:isMaximized(); }
    QRect windowedGeometry() const;
    WindowSizePolicy sizePolicy() const { return sizePolicy_; }
    double lastVideoScale() const { return lastVideoScale_; }
    void setWindowSizing(WindowSizePolicy policy, double scale);

signals:
    void windowSizingChanged();

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void openFile();
    void openFiles(bool append);
    void addFolder();
    QMenu *setupTopBar();
    void updateWindowButtons();
    void enableResizeFrame();
    void setupPlaylist();
    void updatePlaylistActions();
    void ingestUrls(const QList<QUrl> &urls, bool replace, int row = -1);
    void toggleFullscreen();
    void updateMedia(Player::State state, const QString &path, const QString &error);
    void updatePresentation();
    void showLicenses();
    void showShortcuts();
    void gotoTime();
    void revealControls();
    void hideControls();
    void updateTimeline();
    void updateRotation();
    void watchWindowScreen();
    void scheduleVideoResize(bool explicitRequest = false);
    void resizeForVideo();

    Player &player_;
    VideoWidget *video_ = nullptr;
    VideoSurface *surface_ = nullptr;
    ControlBar *controls_ = nullptr;
    QWidget *topBar_ = nullptr;
    QLabel *title_ = nullptr, *zoomLabel_ = nullptr, *rateLabel_ = nullptr;
    QToolButton *menuButton_ = nullptr, *minimize_ = nullptr, *maximize_ = nullptr, *close_ = nullptr;
    PlayerActions *actions_ = nullptr;
    QSplitter *splitter_ = nullptr;
    QList<QMenu *> rotationMenus_;
    PlaylistController *playlist_ = nullptr;
    PlaylistPanel *panel_ = nullptr;
    PlaylistHost *playlistHost_ = nullptr;
    FolderScanner *scanner_ = nullptr;
    Player::State state_ = Player::State::Idle;
    QString path_, error_;
    QSize videoSize_;
    WindowSizePolicy sizePolicy_ = WindowSizePolicy::LastGeometry;
    double lastVideoScale_ = 1;
    QPointer<QWindow> watchedWindow_;
    QMetaObject::Connection screenChanged_, workAreaChanged_;
    QPointer<QScreen> sizingScreen_;
    bool explicitResize_ = false, applyingVideoResize_ = false;
    bool videoResizeDeferred_ = false, movingWindow_ = false;
    int playlistDockExtent_ = 0;
    QRect geometryBeforeFullscreen_;
    int duration_ = 0, position_ = 0;
    double durationSeconds_ = 0;
    bool paused_ = false, wasMaximized_ = false;
    bool audioAvailable_ = false;
    bool maximizePressed_ = false;
    QTimer hideTimer_, videoResizeTimer_;
};
