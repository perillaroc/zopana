#pragma once

#include <QObject>
#include <QPointer>
#include <array>

class QAction;
class QWidget;

class PlayerActions final : public QObject
{
    Q_OBJECT
public:
    enum Id { Open, Retry, PlayPause, ResetView, Fullscreen, Escape,
              FeatureMode, StandardMode, Quit, About, Licenses, Count };
    PlayerActions(QWidget &window, QWidget &video, QWidget &surface);
    QAction *get(Id id) const { return actions_[id]; }
    void setMediaState(bool loaded, bool paused, bool replayable, bool hasVideo, bool retryable);
    void setFullscreen(bool fullscreen);
    void refreshIcons();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool watching() const;
    QWidget &window_, &video_, &surface_;
    std::array<QAction *, Count> actions_{};
    QPointer<QObject> composing_;
};
