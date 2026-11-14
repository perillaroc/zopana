#pragma once

#include "repeatmode.h"

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QKeySequence>
#include <array>

class QAction;
class QKeyEvent;
class QMenu;
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
              OrderSequential, OrderSingle, OrderList, OrderRandom, RepeatCycle, ShuffleToggle,
              AudioNext, AudioDelayLess, AudioDelayMore, AudioDelayReset,
              SubLoad, SubToggle, SubNext, SubDelayLess, SubDelayMore, SubDelayReset, SubScaleUp, SubScaleDown, SubScaleReset,
              RateLess, RateMore, RateReset, RateHalf, RateThreeQuarter, Rate125, Rate150, Rate200, Rate300, Pitch,
              FrameForward, LoopA, LoopB, LoopClear, CaptureSource, CaptureViewport, CaptureFolder, MediaInfo, FileLocation, AlwaysOnTop, Settings, ClearHistory,
              SkipToggle, SkipIntro, SkipOutro, SkipSettings,
              RotateNext, RotatePrevious, RotateReset, Scale50, Scale100, Scale150, Scale200, ScaleFit, Count };
    PlayerActions(QWidget &window, QWidget &video, QWidget &surface);
    QAction *get(Id id) const { return actions_[id]; }
    QAction *repeatModeAction(RepeatMode mode) const;
    void addPlaybackOrderActions(QMenu &menu) const;
    void setPlaybackOrderState(RepeatMode mode, bool shuffleEnabled);
    void setMediaState(bool loaded, bool paused, bool replayable, bool hasVideo, bool retryable);
    void setFullscreen(bool fullscreen);
    void refreshIcons();
    void setSeekable(bool seekable);
    void setSeekSteps(int small, int medium, int large);
    static bool isRelativeSeek(Id id) { return id >= BackSmall && id <= ForwardLarge; }
    bool viewingInputAllowed() const;
    QAction *timelineAction(const QKeyEvent &event) const;
    QString timelineDescription() const;
    void setAudioAvailable(bool available);
    void setMuted(bool muted);
    void attachPlaylist(QWidget &window, QWidget &view);
    QVariantMap defaultBindings() const { return defaults_; }
    static QString normalizedKey(const QString &key);
    QString bindingError(const QString &id,const QStringList &keys) const;
    QStringList conflicts(const QString &id,const QStringList &keys,const QVariantMap &overrides) const;
    QVariantMap resolvedBindings(const QVariantMap &overrides, QStringList *ignored = nullptr) const;
    QStringList applyBindings(const QVariantMap &overrides);

signals:
    void invoked();
    void bindingsChanged();
    void seekStepsChanged();
    void seekRelativeRequested(int seconds);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QStringList bindingConflicts(const QString &id, const QStringList &keys, const QVariantMap &bindings) const;
    bool watching() const;
    QWidget &window_, &video_, &surface_;
    std::array<QAction *, Count> actions_{};
    std::array<QString, Count> appliedIconNames_;
    qint64 iconPaletteKey_ = 0;
    QPointer<QObject> composing_;
    QPointer<QWidget> queue_, playlistWindow_;
    QVariantMap defaults_;
    std::array<int, 3> seekSteps_{};
};
