#pragma once

#include <QObject>
#include <QHash>
#include <QSize>
#include <QString>
#include <QTimer>

#include <mpv/client.h>

#include <optional>

class Player final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Loading, Loaded, Ended, Error };

    explicit Player(QObject *parent = nullptr);
    ~Player() override;
    bool initialize();
    mpv_handle *handle() const { return handle_; }
    void setRendererReady(bool ready);
    void openFile(const QString &path, quint64 itemId = 0);
    quint64 itemId() const { return itemId_; }
    uint64_t generation() const { return generation_; }
    void setPaused(bool paused);
    void togglePaused();
    void stop(bool clearFile = false);
    void seek(double seconds);
    void seekRelative(double seconds);
    double lastPosition() const;
    double positionTarget() const { return seekTarget_.value_or(position_); }
    void setVolume(double volume);
    void setMuted(bool muted);
    void toggleMuted() { setMuted(!muteTarget_); }
    void addSubtitle(const QString &path);
    bool muted() const { return reportedMute_; }
    double volumeTarget() const { return volumeTarget_; }
    void shutdown();

signals:
    void sessionChanged(quint64 generation, quint64 itemId, Player::State state, const QString &error);
    void fileOpening();
    void mediaChanged(Player::State state, const QString &path, const QString &error);
    void durationChanged(double seconds);
    void positionChanged(double seconds);
    void pausedChanged(bool paused);
    void volumeChanged(double volume);
    void seekableChanged(bool seekable);
    void audioAvailableChanged(bool available);
    void mutedChanged(bool muted);
    void seekRequested(double seconds);
    void subtitleLoaded(const QString &path);
    void videoSizeChanged(QSize size);
    void commandError(const QString &message);

private:
    void queueLoad();
    void submitPendingLoad();
    enum class Command { Load, Seek, Pause, Volume, Mute, Stop, Subtitle, ResetLoop };
    struct Request { Command command; uint64_t generation; QString subject; };
    bool submitControl(Command command, const char **args);
    void flushSeek();
    void finishSeek();
    void clearSeek();
    void drainEvents();
    void handleReply(const mpv_event &event);
    void syncFileState();
    void propertyChanged(const mpv_event &event);
    void changeState(State state, const QString &error = {});

    mpv_handle *handle_ = nullptr;
    bool closing_ = false;
    bool rendererReady_ = false;
    bool loadPending_ = false;
    bool stopPending_ = false;
    bool resetLoopsPending_ = false;
    uint64_t generation_ = 0, nextRequest_ = 1;
    quint64 itemId_ = 0;
    QHash<uint64_t, Request> requests_;
    QTimer seekTimer_;
    std::optional<double> seekTarget_, pendingSeek_;
    bool seekBusy_ = false, seekReplied_ = false, seekRestarted_ = false;
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
    int64_t selectedEntry_ = 0; // ID returned by the latest loadfile command.
    int64_t eventEntry_ = 0;    // ID from START_FILE, also identifies FILE_LOADED.
    State eventState_ = State::Idle;
    QString eventError_;
};
