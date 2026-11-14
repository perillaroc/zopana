#pragma once

#include "player.h"
#include <QMainWindow>
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
class QToolButton;
class QUrl;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(Player &player, QWidget *parent = nullptr);
    bool videoContextValid() const;
    bool restoreMaximized() const { return isFullScreen()?wasMaximized_:isMaximized(); }
    QRect windowedGeometry() const;

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

    Player &player_;
    VideoWidget *video_ = nullptr;
    VideoSurface *surface_ = nullptr;
    ControlBar *controls_ = nullptr;
    QWidget *topBar_ = nullptr;
    QLabel *title_ = nullptr, *zoomLabel_ = nullptr, *rateLabel_ = nullptr;
    QToolButton *menuButton_ = nullptr, *minimize_ = nullptr, *maximize_ = nullptr, *close_ = nullptr;
    PlayerActions *actions_ = nullptr;
    QSplitter *splitter_ = nullptr;
    PlaylistController *playlist_ = nullptr;
    PlaylistPanel *panel_ = nullptr;
    PlaylistHost *playlistHost_ = nullptr;
    FolderScanner *scanner_ = nullptr;
    Player::State state_ = Player::State::Idle;
    QString path_, error_;
    QSize videoSize_;
    QRect geometryBeforeFullscreen_;
    int duration_ = 0, position_ = 0;
    double durationSeconds_ = 0;
    bool paused_ = false, wasMaximized_ = false;
    bool audioAvailable_ = false;
    bool maximizePressed_ = false;
    QTimer hideTimer_;
};
