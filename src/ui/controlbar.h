#pragma once

#include <QWidget>
#include <QVariant>

class QAction;
class QLabel;
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
    void setVolume(double volume);
    void setMuted(bool muted);
    void setAudioAvailable(bool available);
    void setLoop(const QVariant &a, const QVariant &b);
    bool cancelSeekPreview();
    void refreshIcons();

signals:
    void seekRequested(int seconds);
    void volumeRequested(int percent);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showTime(int seconds);
    void updateSeekDescription();
    QToolButton *button(QAction *action, const QString &name, QWidget *parent = nullptr);
    PlayerActions &actions_;
    SeekSlider *seek_;
    QSlider *volume_;
    QLabel *current_, *duration_;
    QToolButton *play_, *volumeButton_;
    bool muted_ = false;
    bool audioAvailable_ = false;
    double volumeValue_ = 100;
};
