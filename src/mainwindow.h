#pragma once

#include "player.h"
#include <QMainWindow>
#include <QTimer>

class ControlBar;
class PlayerActions;
class VideoSurface;
class VideoWidget;

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

private:
    void openFile();
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
    Player::State state_ = Player::State::Idle;
    QString path_, error_;
    QSize videoSize_;
    int duration_ = 0, position_ = 0;
    double durationSeconds_ = 0;
    bool paused_ = false, seekable_ = false, wasMaximized_ = false;
    bool audioAvailable_ = false;
    QTimer hideTimer_;
};
