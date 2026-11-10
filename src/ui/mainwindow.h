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
class QUrl;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(Player &player, QWidget *parent = nullptr);
    bool videoContextValid() const;

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void openFile();
    void openFiles(bool append);
    void addFolder();
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
    void updateSeekActions();

    Player &player_;
    VideoWidget *video_ = nullptr;
    VideoSurface *surface_ = nullptr;
    ControlBar *controls_ = nullptr;
    PlayerActions *actions_ = nullptr;
    QSplitter *splitter_ = nullptr;
    PlaylistController *playlist_ = nullptr;
    PlaylistPanel *panel_ = nullptr;
    PlaylistHost *playlistHost_ = nullptr;
    FolderScanner *scanner_ = nullptr;
    Player::State state_ = Player::State::Idle;
    QString path_, error_;
    QSize videoSize_;
    int duration_ = 0, position_ = 0;
    double durationSeconds_ = 0;
    bool paused_ = false, seekable_ = false, wasMaximized_ = false;
    bool audioAvailable_ = false;
    QTimer hideTimer_;
};
