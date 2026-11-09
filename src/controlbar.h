#pragma once

#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QSlider;
class QToolButton;
class PlayerActions;

class ControlBar final : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(PlayerActions &actions, QWidget *parent = nullptr);
    void setTimeline(int position, int duration, bool seekable);
    void setStatus(const QString &status, const QString &videoSize);
    void setVolume(double volume);
    void refreshIcons();
    QMenu *volumeMenu() const { return volumeMenu_; }

signals:
    void seekRequested(int seconds);
    void volumeRequested(int percent);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showTime(int seconds);
    QToolButton *button(QAction *action, const QString &name, QWidget *parent = nullptr);
    PlayerActions &actions_;
    QSlider *seek_, *volume_, *popupVolume_;
    QLabel *current_, *duration_, *status_, *videoSize_, *volumeLabel_;
    QWidget *info_;
    QToolButton *open_, *play_, *reset_, *volumeButton_, *more_;
    QMenu *volumeMenu_;
};
