#pragma once

#include "player.h"

#include <QMainWindow>

class QLabel;
class QPushButton;
class QSlider;
class VideoWidget;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(Player &player, QWidget *parent = nullptr);
    bool videoContextValid() const;

private:
    void openFile();
    void toggleFullscreen();
    void updateMedia(Player::State state, const QString &path, const QString &error);
    void showTime(int position);

    Player &player_;
    VideoWidget *video_;
    QPushButton *playPause_;
    QPushButton *resetView_;
    QPushButton *fullscreen_;
    QSlider *seek_;
    QLabel *time_;
    QLabel *status_;
    QLabel *videoSize_;

    bool loaded_ = false;
    bool paused_ = false;
    bool wasMaximized_ = false;
};
