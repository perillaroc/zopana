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
              FeatureMode, StandardMode, Quit, About, Licenses,
              Stop, BackSmall, ForwardSmall, BackMedium, ForwardMedium, BackLarge, ForwardLarge,
              SeekStart, SeekEnd, SeekGoto, VolumeUp, VolumeDown, Mute, InputMode, ZoomIn, ZoomOut, Shortcuts,
              Add, AddFolder, CancelScan, Previous, Next, Playlist, PlaylistMode, ClosePlaylist,
              QueuePlay, QueueRemove, QueueUp, QueueDown, QueueClear,
              OrderSequential, OrderSingle, OrderList, OrderRandom, Count };
    PlayerActions(QWidget &window, QWidget &video, QWidget &surface);
    QAction *get(Id id) const { return actions_[id]; }
    void setMediaState(bool loaded, bool paused, bool replayable, bool hasVideo, bool retryable);
    void setFullscreen(bool fullscreen);
    void refreshIcons();
    void setSeekable(bool seekable);
    void setAudioAvailable(bool available);
    void setMuted(bool muted);
    void attachPlaylist(QWidget &window, QWidget &view);

signals:
    void invoked();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool watching() const;
    QWidget &window_, &video_, &surface_;
    std::array<QAction *, Count> actions_{};
    QPointer<QObject> composing_;
    QPointer<QWidget> queue_, playlistWindow_;
};
