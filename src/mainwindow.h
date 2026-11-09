#pragma once

#include "player.h"
#include <QMainWindow>

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

private:
    void openFile();
    void toggleFullscreen();
    void updateMedia(Player::State state, const QString &path, const QString &error);
    void updatePresentation();
    void showLicenses();

    Player &player_;
    VideoWidget *video_ = nullptr;
    VideoSurface *surface_ = nullptr;
    ControlBar *controls_ = nullptr;
    PlayerActions *actions_ = nullptr;
    Player::State state_ = Player::State::Idle;
    QString path_, error_;
    QSize videoSize_;
    int duration_ = 0, position_ = 0;
    bool paused_ = false, seekable_ = false, wasMaximized_ = false;
};
