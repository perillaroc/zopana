#include "player.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
bool check(const char *operation, int result)
{
    if (result >= 0)
        return true;
    std::fprintf(stderr, "%s: %d (%s)\n", operation, result, mpv_error_string(result));
    return false;
}

// libmpv returns loadfile results and video/audio parameters as named node maps.
int64_t mapInteger(const mpv_node &node, const char *key)
{
    if (node.format == MPV_FORMAT_NODE_MAP) {
        const mpv_node_list &list = *node.u.list;
        for (int i = 0; i < list.num; ++i) {
            if (std::strcmp(list.keys[i], key) == 0 && list.values[i].format == MPV_FORMAT_INT64)
                return list.values[i].u.int64;
        }
    }
    return 0;
}

struct Property {
    const char *name;
    mpv_format format;
};

constexpr Property fileProperties[] = {
    {"duration", MPV_FORMAT_DOUBLE}, {"time-pos", MPV_FORMAT_DOUBLE},
    {"pause", MPV_FORMAT_FLAG}, {"video-out-params", MPV_FORMAT_NODE},
    {"current-ao", MPV_FORMAT_STRING}, {"audio-out-params", MPV_FORMAT_NODE},
    {"seekable", MPV_FORMAT_FLAG}, {"estimated-vf-fps", MPV_FORMAT_DOUBLE}
};
} // namespace

Player::Player(QObject *parent) : QObject(parent)
{
    seekTimer_.setSingleShot(true);
    seekTimer_.setInterval(35);
    connect(&seekTimer_, &QTimer::timeout, this, &Player::flushSeek);
}

Player::~Player()
{
    shutdown();
    if (handle_)
        mpv_terminate_destroy(handle_);
}

bool Player::initialize()
{
    auto logVersion = [](const char *source, unsigned long version) {
        std::fprintf(stderr, "libmpv client API (%s): %lu.%lu\n", source, version >> 16, version & 0xffffUL);
    };
    logVersion("headers", MPV_CLIENT_API_VERSION);
    logVersion("runtime", mpv_client_api_version());

    handle_ = mpv_create();
    if (!handle_) {
        std::fputs("mpv_create failed\n", stderr);
        return false;
    }
    const char *options[][2] = {
        {"vo", "libmpv"}, {"hwdec", "no"}, {"osc", "no"},
        {"input-default-bindings", "no"}, {"idle", "yes"},
        {"keepaspect", "yes"}, {"video-unscaled", "no"}, {"panscan", "0"},
        {"video-align-x", "0"}, {"video-align-y", "0"},
        {"video-margin-ratio-left", "0"}, {"video-margin-ratio-right", "0"},
        {"video-margin-ratio-top", "0"}, {"video-margin-ratio-bottom", "0"},
        {"video-zoom", "0"}, {"video-pan-x", "0"}, {"video-pan-y", "0"}
    };
    for (const auto &option : options) {
        if (!check(option[0], mpv_set_option_string(handle_, option[0], option[1])))
            return false;
    }
    if (!check("mpv_initialize", mpv_initialize(handle_))
        || !check("observe volume", mpv_observe_property(handle_, 0, "volume", MPV_FORMAT_DOUBLE))
        || !check("observe mute", mpv_observe_property(handle_, 0, "mute", MPV_FORMAT_FLAG)))
        return false;

    std::fputs("libmpv view: video-zoom=0,video-pan-x=0,video-pan-y=0 (fixed)\n", stderr);
    mpv_get_property(handle_, "audio-buffer", MPV_FORMAT_DOUBLE, &audioBuffer_);
    mpv_set_wakeup_callback(handle_, [](void *data) {
        auto *player = static_cast<Player *>(data);
        QMetaObject::invokeMethod(player, &Player::drainEvents, Qt::QueuedConnection);
    }, this);
    return true;
}

void Player::shutdown()
{
    if (closing_)
        return;
    closing_ = true;
    clearSeek();
    // libmpv serializes callback replacement with calls to the old callback.
    // Posted Qt invocations may still arrive, but drainEvents now exits immediately.
    if (handle_)
        mpv_set_wakeup_callback(handle_, nullptr, nullptr);
}

void Player::setRendererReady(bool ready)
{
    if (closing_ || rendererReady_ == ready)
        return;
    rendererReady_ = ready;
    if (ready) {
        submitPendingLoad();
    } else if (state_ == State::Loaded || state_ == State::Loading) {
        // Freeing an active render context disables its video output. Reload
        // at the last reported position/pause state once the new VO is usable.
        queueLoad();
    }
}

void Player::openFile(const QString &path)
{
    if (closing_ || !handle_ || path.isEmpty())
        return;
    emit fileOpening();
    path_ = path;
    position_ = 0;
    paused_ = false;
    queueLoad();
}

void Player::queueLoad()
{
    ++generation_;
    clearSeek();
    seekable_ = false; duration_ = fps_ = sampleRate_ = 0;
    emit audioAvailableChanged(false);
    if (selectedEntry_)
        mpv_unobserve_property(handle_, selectedEntry_);
    selectedEntry_ = 0;
    loadPending_ = true;
    stopPending_ = false;
    changeState(State::Loading);
    submitPendingLoad();
}

void Player::submitPendingLoad()
{
    // Complete earlier commands before replacing the entry. In particular, a
    // previous file's outstanding seek must never execute on the new file.
    if (closing_ || !requests_.isEmpty())
        return;
    if (stopPending_) {
        stopPending_ = false;
        const char *args[] = {"stop", "keep-playlist", nullptr};
        submitControl(Command::Stop, args);
        return;
    }
    if (!rendererReady_ || !loadPending_)
        return;
    loadPending_ = false;
    const QByteArray path = path_.toUtf8();
    const QByteArray options = QByteArray("pause=") + (paused_ ? "yes" : "no")
                             + ",start=" + QByteArray::number(position_, 'f', 3);
    const char *args[] = {"loadfile", path.constData(), "replace", "-1", options.constData(), nullptr};
    if (!submitControl(Command::Load, args)) {
        changeState(State::Error, QStringLiteral("无法提交打开请求"));
        return;
    }
    std::fprintf(stderr, "loadfile submitted: %s (%s)\n", path.constData(), options.constData());
}

void Player::setPaused(bool paused)
{
    if (state_ != State::Loaded)
        return;
    const char *args[] = {"set", "pause", paused ? "yes" : "no", nullptr};
    submitControl(Command::Pause, args);
}

void Player::togglePaused()
{
    if (state_ == State::Loaded) {
        const char *args[] = {"cycle", "pause", nullptr};
        submitControl(Command::Pause, args);
    }
}

void Player::stop()
{
    if (!handle_ || closing_ || path_.isEmpty())
        return;
    ++generation_;
    clearSeek();
    if (selectedEntry_) mpv_unobserve_property(handle_, selectedEntry_);
    selectedEntry_ = 0;
    loadPending_ = false; stopPending_ = true;
    position_ = 0; paused_ = false; seekable_ = false;
    emit audioAvailableChanged(false);
    changeState(State::Idle);
    submitPendingLoad();
}

double Player::lastPosition() const
{
    // Video: reserve a decoded frame interval. Audio: reserve two configured
    // output buffers: the decoder can reach EOF during paused output priming.
    const double interval = fps_ > 0 ? 1.0 / fps_
        : sampleRate_ > 0 && audioBuffer_ > 0 ? std::max(2 * audioBuffer_, 1.0 / sampleRate_) : 0;
    return duration_ > 0 && interval > 0 ? std::max(0.0, duration_ - interval) : 0;
}

void Player::clearSeek()
{
    seekTimer_.stop(); pendingSeek_.reset(); seekTarget_.reset();
    seekBusy_ = seekReplied_ = seekRestarted_ = false;
}

void Player::seek(double seconds)
{
    if (state_ != State::Loaded || !seekable_ || duration_ <= 0 || !std::isfinite(seconds) || closing_)
        return;
    const double maximum = lastPosition();
    if (maximum <= 0 && seconds > 0) return;
    seekTarget_ = pendingSeek_ = std::clamp(seconds, 0.0, maximum);
    emit seekRequested(*seekTarget_);
    if (!seekTimer_.isActive()) seekTimer_.start();
}

void Player::seekRelative(double seconds)
{
    if (std::isfinite(seconds)) seek(positionTarget() + seconds);
}

void Player::flushSeek()
{
    if (!pendingSeek_ || seekBusy_ || state_ != State::Loaded) return;
    const double seconds = *pendingSeek_; pendingSeek_.reset();
    const QByteArray target = QByteArray::number(seconds, 'f', 6);
    const char *args[] = {"seek", target.constData(), "absolute+exact", nullptr};
    std::fprintf(stderr, "seek submitted: %s s\n", target.constData());
    seekReplied_ = seekRestarted_ = false;
    seekBusy_ = submitControl(Command::Seek, args);
    if (!seekBusy_) clearSeek();
}

void Player::finishSeek()
{
    if (!seekBusy_ || !seekReplied_ || !seekRestarted_) return;
    seekBusy_ = false;
    if (pendingSeek_) seekTimer_.start();
    else seekTarget_.reset();
}

void Player::setVolume(double volume)
{
    if (!std::isfinite(volume))
        return;
    if (muteTarget_) setMuted(false);
    // Keep fractional wheel input between events, including while a previous
    // slider or wheel request is still waiting for its asynchronous reply.
    volumeTarget_ = std::clamp(volume, 0.0, 100.0);
    if (volumeInFlight_)
        return;
    const QByteArray value = QByteArray::number(volumeTarget_, 'g', 15);
    const char *args[] = {"set", "volume", value.constData(), nullptr};
    if (submitControl(Command::Volume, args))
        volumeInFlight_ = volumeTarget_;
    else {
        volumeTarget_ = reportedVolume_;
        emit volumeChanged(reportedVolume_);
    }
}

void Player::setMuted(bool muted)
{
    muteTarget_ = muted;
    if (muteInFlight_) return;
    const char *args[] = {"set", "mute", muted ? "yes" : "no", nullptr};
    if (submitControl(Command::Mute, args)) muteInFlight_ = muted;
    else { muteTarget_ = reportedMute_; emit mutedChanged(reportedMute_); }
}

void Player::addSubtitle(const QString &path)
{
    if (state_ != State::Loaded) return;
    const QByteArray filename = path.toUtf8();
    const char *args[] = {"sub-add", filename.constData(), "select", nullptr};
    submitControl(Command::Subtitle, args);
}

bool Player::submitControl(Command command, const char **args)
{
    if (!handle_ || closing_)
        return false;
    const uint64_t request = nextRequest_++;
    const int result = mpv_command_async(handle_, request, args);
    if (check(args[0], result)) {
        requests_.insert(request, {command, generation_, command == Command::Subtitle ? QString::fromUtf8(args[1]) : QString()});
        return true;
    }
    emit commandError(QStringLiteral("%1: %2").arg(args[0], mpv_error_string(result)));
    return false;
}

void Player::changeState(State state, const QString &error)
{
    if (state != State::Loaded) clearSeek();
    state_ = state;
    emit mediaChanged(state, path_, error);
}

void Player::drainEvents()
{
    while (!closing_) {
        const mpv_event &event = *mpv_wait_event(handle_, 0);
        switch (event.event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_COMMAND_REPLY:
            handleReply(event);
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            if (state_ == State::Loaded && eventEntry_ == selectedEntry_) {
                seekRestarted_ = true; finishSeek();
            }
            break;
        case MPV_EVENT_START_FILE:
            eventEntry_ = static_cast<mpv_event_start_file *>(event.data)->playlist_entry_id;
            eventState_ = State::Loading;
            eventError_.clear();
            break;
        case MPV_EVENT_FILE_LOADED:
            eventState_ = State::Loaded;
            syncFileState();
            break;
        case MPV_EVENT_END_FILE: {
            const auto &end = *static_cast<mpv_event_end_file *>(event.data);
            std::fprintf(stderr, "END_FILE entry=%lld reason=%d error=%s\n",
                         static_cast<long long>(end.playlist_entry_id), end.reason, mpv_error_string(end.error));
            if (end.playlist_entry_id != eventEntry_)
                break;
            switch (end.reason) {
            case MPV_END_FILE_REASON_EOF: eventState_ = State::Ended; break;
            case MPV_END_FILE_REASON_ERROR:
                eventState_ = State::Error;
                eventError_ = QString::fromUtf8(mpv_error_string(end.error));
                break;
            default: eventState_ = State::Idle; break;
            }
            syncFileState();
            break;
        }
        case MPV_EVENT_PROPERTY_CHANGE:
            propertyChanged(event);
            break;
        default:
            break;
        }
    }
}

void Player::handleReply(const mpv_event &event)
{
    auto found = requests_.find(event.reply_userdata);
    if (found == requests_.end()) return;
    const Request request = found.value(); requests_.erase(found);
    const bool current = request.generation == generation_;
    if (request.command == Command::Load) {
        if (current && !loadPending_) {
            if (!check("loadfile reply", event.error)) {
                changeState(State::Error, QString::fromUtf8(mpv_error_string(event.error)));
            } else {
                const auto &result = static_cast<mpv_event_command *>(event.data)->result;
                selectedEntry_ = mapInteger(result, "playlist_entry_id");
                if (!selectedEntry_)
                    changeState(State::Error, QStringLiteral("libmpv returned no playlist entry"));
                else
                    syncFileState(); // START/LOADED and the command reply may arrive separately.
            }
        }
    } else {
        if (!check("command reply", event.error) && current)
            emit commandError(QString::fromUtf8(mpv_error_string(event.error)));
        if (request.command == Command::Seek && current) {
            seekReplied_ = true;
            if (event.error < 0) { clearSeek(); emit positionChanged(position_); } else finishSeek();
        }
        if (request.command == Command::Subtitle && current && event.error >= 0) emit subtitleLoaded(request.subject);
        if (request.command == Command::Volume) {
            const bool newerTarget = volumeTarget_ != volumeInFlight_.value();
            volumeInFlight_.reset();
            if (newerTarget)
                setVolume(volumeTarget_);
            else if (event.error < 0) {
                volumeTarget_ = reportedVolume_;
                emit volumeChanged(reportedVolume_);
            }
        }
        if (request.command == Command::Mute) {
            const bool newerTarget = muteTarget_ != muteInFlight_.value();
            muteInFlight_.reset();
            if (newerTarget) setMuted(muteTarget_);
            else if (event.error < 0) { muteTarget_ = reportedMute_; emit mutedChanged(reportedMute_); }
        }
    }
    submitPendingLoad();
}

void Player::syncFileState()
{
    if (eventEntry_ != selectedEntry_ || !selectedEntry_ || eventState_ == state_)
        return;
    if (eventState_ == State::Loaded) {
        std::fprintf(stderr, "FILE_LOADED entry=%lld: %s\n", static_cast<long long>(selectedEntry_),
                     path_.toUtf8().constData());
        // Re-observe on every file to receive its initial values, including
        // properties whose value is identical to the previous file's.
        for (const auto &property : fileProperties) {
            const int result = mpv_observe_property(handle_, selectedEntry_, property.name, property.format);
            if (!check(property.name, result))
                emit commandError(QStringLiteral("%1: %2").arg(property.name, mpv_error_string(result)));
        }
    }
    changeState(eventState_, eventError_);
}

void Player::propertyChanged(const mpv_event &event)
{
    const auto &property = *static_cast<mpv_event_property *>(event.data);
    if (property.format == MPV_FORMAT_NONE || !property.data)
        return;
    const QByteArrayView name(property.name);
    if (name == "volume") {
        const double volume = *static_cast<double *>(property.data);
        reportedVolume_ = volume;
        if (!volumeInFlight_)
            volumeTarget_ = volume;
        std::fprintf(stderr, "volume: %.2f\n", volume);
        emit volumeChanged(volume);
        return;
    }
    if (name == "mute") {
        reportedMute_ = *static_cast<int *>(property.data) != 0;
        if (!muteInFlight_) muteTarget_ = reportedMute_;
        emit mutedChanged(reportedMute_);
        return;
    }
    if (state_ != State::Loaded || event.reply_userdata != static_cast<uint64_t>(selectedEntry_))
        return;
    if (name == "seekable") {
        seekable_ = *static_cast<int *>(property.data) != 0;
        emit seekableChanged(seekable_);
    } else if (name == "estimated-vf-fps") {
        fps_ = *static_cast<double *>(property.data);
    } else if (name == "duration") {
        const double seconds = *static_cast<double *>(property.data);
        duration_ = seconds;
        std::fprintf(stderr, "duration: %.3f s\n", seconds);
        emit durationChanged(seconds);
    } else if (name == "time-pos") {
        position_ = *static_cast<double *>(property.data);
        emit positionChanged(position_);
    } else if (name == "pause") {
        paused_ = *static_cast<int *>(property.data) != 0;
        std::fprintf(stderr, "pause: %s\n", paused_ ? "yes" : "no");
        emit pausedChanged(paused_);
    } else if (name == "video-out-params") {
        const auto &node = *static_cast<mpv_node *>(property.data);
        // dw/dh include pixel aspect ratio; rotate is the remaining VO rotation.
        QSize size(int(mapInteger(node, "dw")), int(mapInteger(node, "dh")));
        if (mapInteger(node, "rotate") % 180 == 90)
            size.transpose();
        std::fprintf(stderr, "video display size: %dx%d\n", size.width(), size.height());
        emit videoSizeChanged(size);
    } else if (name == "current-ao") {
        std::fprintf(stderr, "audio output: %s\n", *static_cast<char **>(property.data));
    } else if (name == "audio-out-params") {
        const auto &node = *static_cast<mpv_node *>(property.data);
        sampleRate_ = double(mapInteger(node, "samplerate"));
        emit audioAvailableChanged(sampleRate_ > 0);
        std::fprintf(stderr, "audio output: %lld Hz, %lld channels\n",
                     static_cast<long long>(mapInteger(node, "samplerate")),
                     static_cast<long long>(mapInteger(node, "channel-count")));
    }
}
