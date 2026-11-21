#pragma once

#include <QObject>
#include <QHash>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QImage>
#include <QSet>
#include "mediastate.h"

#include <mpv/client.h>

#include <optional>

class Player final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Loading, Loaded, Ended, Error };
    enum class CompletionReason { NaturalEof, SkippedOutro };
    enum class StartPosition { Resume, SkipIntro, Beginning, User };
    struct PlaybackIdentity {
        quint64 generation = 0, renderEpoch = 0, itemId = 0;
        qint64 entry = 0;
        bool operator==(const PlaybackIdentity &) const = default;
    };

    explicit Player(QObject *parent = nullptr);
    ~Player() override;
    bool initialize();
    mpv_handle *handle() const { return handle_; }
    void setRendererReady(bool ready);
    void openFile(const QString &path, quint64 itemId = 0, const QStringList &subtitles = {}, bool automatic = false);
    quint64 itemId() const { return itemId_; }
    uint64_t generation() const { return generation_; }
    quint64 renderEpoch() const { return renderEpoch_; }
    PlaybackIdentity playbackIdentity() const { return {generation_, renderEpoch_, itemId_, selectedEntry_}; }
    bool ownsCompletion(const PlaybackIdentity &playback) const;
    State state() const { return state_; }
    QString path() const { return path_; }
    bool recovering() const { return recovering_; }
    bool lastLoadWasRecovery() const { return lastLoadWasRecovery_; }
    const QVector<MediaTrack> &tracks() const { return tracks_; }
    const QVector<MediaChapter> &chapters() const { return chapters_; }
    qsizetype currentChapter() const;
    QVariant mediaProperty(const QString &name) const { return properties_.value(name); }
    QVariant propertyTarget(const QString &name) const { return targets_.value(name, properties_.value(name)); }
    void setMediaProperty(const QString &name, const QVariant &value);
    void adjustMediaProperty(const QString &name, double delta);
    void selectTrack(const QString &type, qlonglong id, quint64 generation);
    void nextTrack(const QString &type);
    void frameStep();
    void frameBackStep();
    bool canFrameBackStep() const;
    bool frameBackStepInProgress() const { return submittedSeek_ && submittedSeek_->backwardFrame; }
    void setLoopA();
    void setLoopB();
    void clearLoop();
    bool loopActive() const;
    void captureSource();
    void setPaused(bool paused);
    void togglePaused();
    void stop(bool clearFile = false);
    void seek(double seconds);
    void seekFrom(double seconds, double origin);
    bool seekToStart(double seconds, StartPosition reason);
    void cancelStartSeek();
    void seekRelative(double seconds, bool continuing = false);
    void undoSeek();
    bool completeOutro(PlaybackIdentity playback);
    double lastPosition() const;
    bool seekable() const { return seekable_; }
    bool canSeek() const;
    bool seekInProgress() const { return pendingSeek_.has_value() || submittedSeek_.has_value(); }
    bool canUndoSeek() const { return canSeek() && undoPosition_.has_value() && !seekInProgress(); }
    bool canSetLoopPoint() const { return canSeek() && !seekInProgress(); }
    double positionTarget() const;
    double position() const { return position_; }
    double duration() const { return duration_; }
    bool paused() const { return paused_; }
    int videoRotation() const { return videoRotation_; }
    quint64 videoTransformVersion() const { return videoTransformVersion_; }
    bool videoTransformReady() const { return videoOutputReady_ && state_ == State::Loaded && !recovering_ && rendererReady_; }
    void setInitialPaused(bool paused) { if (state_ == State::Loading && !recovering_) pauseTarget_ = paused; }
    void holdStart(bool held) { startHeld_ = held; setInitialPaused(held); }
    void setTrackLanguages(const QString &audio,const QString &subtitles);
    void skipNextResume() { skipResumeNext_ = true; }
    bool resumeAllowed() const { return resumeAllowed_; }
    bool automaticOpen() const { return automaticOpen_; }
    void setVolume(double volume);
    void setAudioPreferences(double volume, bool muted);
    void setMuted(bool muted);
    void toggleMuted() { setMuted(!muteTarget_); }
    void addSubtitle(const QString &path, bool select = true);
    bool muted() const { return reportedMute_; }
    bool muteTarget() const { return muteTarget_; }
    double volumeTarget() const { return volumeTarget_; }
    void shutdown();

signals:
    void detailsChanged();
    void chaptersChanged();
    void preferenceTargetChanged();
    void notice(const QString &message);
    void sourceImageReady(const QImage &image, quint64 generation, quint64 renderEpoch, quint64 transformVersion,
                          int rotation, const QString &path, double position);
    void requestSubmitted(quint64 requestId, quint64 generation, quint64 renderEpoch, const QString &operation);
    void requestFinished(quint64 requestId, bool current, bool success);
    void restored(quint64 generation, quint64 renderEpoch);
    void sessionChanged(quint64 generation, quint64 itemId, Player::State state, const QString &error);
    void playbackCompleted(const Player::PlaybackIdentity &playback, Player::CompletionReason reason);
    void fileOpening();
    void stopping();
    void mediaChanged(Player::State state, const QString &path, const QString &error);
    void durationChanged(double seconds);
    void positionChanged(double seconds);
    void pausedChanged(bool paused);
    void playbackAdvanced(const Player::PlaybackIdentity &playback, double before, double position);
    void volumeChanged(double volume);
    void seekableChanged(bool seekable);
    void audioAvailableChanged(bool available);
    void mutedChanged(bool muted);
    void seekRequested(double seconds);
    void seekSettled(double position);
    void seekActivityChanged();
    void subtitleLoaded(const QString &path);
    void videoSizeChanged(QSize size);
    void videoTransformChanged();
    void commandError(const QString &message);
    void startPlaybackRequested();
    void startPositionResolved(const Player::PlaybackIdentity &playback, Player::StartPosition reason, bool succeeded);

private:
    void queueLoad(bool recovery = false);
    void submitPendingLoad();
    enum class Command { Load, Seek, Pause, Volume, Mute, Stop, Subtitle, ResetLoop,
                         Property, FramePause, Frame, BackStepPause, ReadBackStepPause, Screenshot, RestoreProperty,
                         RestoreSubtitle, RestoreSeek, ReadProperty, ReadSeekPosition, FinishOutro };
    struct Request {
        Command command;
        uint64_t generation, epoch;
        int64_t entry;
        QString subject;
        QVariant value;
        quint64 transformVersion = 0;
        int rotation = 0;
    };
    bool submitControl(Command command, const char **args, const QString &subject = {}, const QVariant &value = {});
    void applyVolume();
    bool issueProperty(const QString &name, const QVariant &value, Command command = Command::Property);
    bool readProperty(const QString &name, Command command = Command::ReadProperty);
    void applyProperty(const QString &name, const QVariant &value);
    void restoreStep();
    void publishFileProperties();
    void updateTracks();
    void updateChapters();
    void resetFileDetails();
    void invalidateVideoOutput(int target);
    void confirmVideoOutput();
    struct Seek { double target; std::optional<double> origin; bool undo = false, backwardFrame = false; };
    void queueSeek(Seek seek);
    void flushSeek();
    void submitSeek(const char **args, Command command = Command::Seek);
    void submitFrameBackStep();
    void finishSeek();
    void clearSeek();
    void settleSeek(std::optional<double> confirmedPosition);
    bool naturalProgress() const;
    void drainEvents();
    void handleReply(const mpv_event &event);
    void syncFileState();
    void propertyChanged(const mpv_event &event);
    void changeState(State state, const QString &error = {});
    void finishCurrentPlayback(PlaybackIdentity playback, CompletionReason reason);

    mpv_handle *handle_ = nullptr;
    bool closing_ = false;
    bool rendererReady_ = false;
    bool loadPending_ = false;
    bool stopPending_ = false;
    bool resetLoopsPending_ = false;
    std::optional<PlaybackIdentity> completedPlayback_;
    uint64_t generation_ = 0, nextRequest_ = 1;
    quint64 renderEpoch_ = 1, observationId_ = 0, nextObservation_ = 1;
    quint64 itemId_ = 0;
    QHash<uint64_t, Request> requests_;
    QTimer seekTimer_, seekTimeout_;
    std::optional<Seek> pendingSeek_, submittedSeek_;
    std::optional<double> relativeSeekOrigin_, undoPosition_;
    struct InitialSeek { PlaybackIdentity playback; StartPosition reason; };
    std::optional<InitialSeek> initialSeek_;
    quint64 seekRequest_ = 0;
    bool seekStarted_ = false, seekRestarted_ = false;
    bool seekable_ = false;
    double duration_ = 0, fps_ = 0, sampleRate_ = 0;
    double audioBuffer_ = 0;
    bool reportedMute_ = false, muteTarget_ = false;
    std::optional<bool> muteInFlight_;
    double volumeTarget_ = 100;
    double reportedVolume_ = 100;
    std::optional<double> volumeInFlight_;

    QString path_;
    State state_ = State::Idle;
    double position_ = 0;
    bool paused_ = false;
    bool pauseTarget_ = false, engineLoaded_ = false;
    bool videoOutputObserved_ = false;
    bool framePending_ = false, startHeld_ = false;
    bool recovering_ = false, lastLoadWasRecovery_ = false;
    int restoreStage_ = 0;
    bool restoreRestarted_ = false;
    struct Recovery { double position = 0; QVariantMap values; QString audio, subtitle; QStringList external; } recovery_;
    QVariantMap properties_{{"speed",1.0},{"audio-pitch-correction",true},{"sub-visibility",true},{"sub-scale",1.0},
                            {"audio-delay",0.0},{"sub-delay",0.0},{"video-rotate",0}};
    QVariantMap targets_;
    QSet<QString> propertyInFlight_;
    QVector<MediaTrack> tracks_;
    QVector<MediaChapter> chapters_;
    QStringList initialSubtitles_;
    QString audioLanguages_,subtitleLanguages_;
    bool trackPreferencesPending_=false,skipResumeNext_=false,resumeAllowed_=true,automaticOpen_=false;
    QSize videoSize_, reportedVideoSize_;
    int videoRotation_ = 0, reportedRotation_ = 0;
    std::optional<int> rotationBase_, expectedRotation_;
    quint64 videoTransformVersion_ = 0;
    bool videoOutputReady_ = false;
    int64_t selectedEntry_ = 0; // ID returned by the latest loadfile command.
    int64_t eventEntry_ = 0;    // ID from START_FILE, also identifies FILE_LOADED.
    State eventState_ = State::Idle;
    QString eventError_;
};
