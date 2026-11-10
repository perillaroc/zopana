#pragma once

#include <QWidget>
#include <QVariant>

class QAction;
class QLabel;
class QMenu;
class QSlider;
class QToolButton;
class SeekSlider;
class PlayerActions;

class ControlBar final : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(PlayerActions &actions, QWidget *parent = nullptr);
    void setTimeline(int position, int duration, bool seekable);
    void setStatus(const QString &status, const QString &videoSize);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setAudioAvailable(bool available);
    void setZoom(double scale);
    void setRate(double rate);
    void setLoop(const QVariant &a, const QVariant &b);
    QMenu *moreMenu() const;
    bool cancelSeekPreview();
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
    SeekSlider *seek_;
    QSlider *volume_, *popupVolume_;
    QLabel *current_, *duration_, *status_, *videoSize_, *volumeLabel_;
    QLabel *zoom_;
    QLabel *rate_;
    QWidget *info_;
    QToolButton *open_, *play_, *reset_, *volumeButton_, *more_;
    QMenu *volumeMenu_;
    bool muted_ = false;
    bool audioAvailable_ = false;
    double volumeValue_ = 100;
};
