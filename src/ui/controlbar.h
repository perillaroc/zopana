#pragma once

#include "mediastate.h"

#include <QWidget>

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
    void setTimeline(double position, double duration, bool seekable);
    void setChapters(const QVector<MediaChapter> &chapters);
    void setCurrentChapter(qsizetype chapter);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setAudioAvailable(bool available);
    void setRateMenu(QMenu *menu);
    void setPlaybackRate(double rate, bool available);
    void setLoop(const QVariant &a, const QVariant &b);
    bool cancelSeekPreview();
    bool seekPreviewActive() const;
    void refreshIcons();

signals:
    void seekRequested(double seconds, double origin);
    void chapterRequested(qsizetype chapter, double origin);
    void volumeRequested(int percent);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showTime(double seconds);
    void updateSeekDescription();
    QToolButton *button(QAction *action, const QString &name, QWidget *parent = nullptr);
    PlayerActions &actions_;
    SeekSlider *seek_;
    QSlider *volume_;
    QLabel *current_, *duration_;
    QToolButton *play_, *volumeButton_, *rateButton_;
    bool muted_ = false;
    bool audioAvailable_ = false;
    double volumeValue_ = 100;
};
