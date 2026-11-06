#pragma once

#include <QObject>
#include <QSize>
#include <QString>

#include <mpv/client.h>

#include <optional>

class Player final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Loading, Loaded, Ended, Error };

    ~Player() override;
    bool initialize();
    mpv_handle *handle() const { return handle_; }
    void setRendererReady(bool ready);
    void openFile(const QString &path);
    void setPaused(bool paused);
    void togglePaused();
    void seek(double seconds);
    void setVolume(double volume);
    double volumeTarget() const { return volumeTarget_; }
    void shutdown();

signals:
    void fileOpening();
    void mediaChanged(Player::State state, const QString &path, const QString &error);
    void durationChanged(double seconds);
    void positionChanged(double seconds);
    void pausedChanged(bool paused);
    void volumeChanged(double volume);
    void videoSizeChanged(QSize size);
    void commandError(const QString &message);

private:
    void queueLoad();
    void submitPendingLoad();
    bool submitControl(uint64_t request, const char **args);
    void drainEvents();
    void handleReply(const mpv_event &event);
    void syncFileState();
    void propertyChanged(const mpv_event &event);
    void changeState(State state, const QString &error = {});

    mpv_handle *handle_ = nullptr;
    bool closing_ = false;
    bool rendererReady_ = false;
    bool loadPending_ = false;
    bool loadInFlight_ = false;
    unsigned controlsInFlight_ = 0;
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
