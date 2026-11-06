#include "player.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
enum Request : uint64_t { Load = 1, Seek, Pause, Volume };

const char *controlName(uint64_t request)
{
    switch (request) {
    case Seek: return "seek";
    case Pause: return "pause";
    case Volume: return "volume";
    default: return "command";
    }
}

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
    {"current-ao", MPV_FORMAT_STRING}, {"audio-out-params", MPV_FORMAT_NODE}
};
} // namespace

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
        || !check("observe volume", mpv_observe_property(handle_, 0, "volume", MPV_FORMAT_DOUBLE)))
        return false;

    std::fputs("libmpv view: video-zoom=0,video-pan-x=0,video-pan-y=0 (fixed)\n", stderr);
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
    if (selectedEntry_)
        mpv_unobserve_property(handle_, selectedEntry_);
    selectedEntry_ = 0;
    loadPending_ = true;
    changeState(State::Loading);
    submitPendingLoad();
}

void Player::submitPendingLoad()
{
    // Complete earlier commands before replacing the entry. In particular, a
    // previous file's outstanding seek must never execute on the new file.
    if (!rendererReady_ || !loadPending_ || loadInFlight_ || controlsInFlight_ || closing_)
        return;
    loadPending_ = false;
    const QByteArray path = path_.toUtf8();
    const QByteArray options = QByteArray("pause=") + (paused_ ? "yes" : "no")
                             + ",start=" + QByteArray::number(position_, 'f', 3);
    const char *args[] = {"loadfile", path.constData(), "replace", "-1", options.constData(), nullptr};
    const int result = mpv_command_async(handle_, Load, args);
    if (!check("loadfile", result)) {
        changeState(State::Error, QString::fromUtf8(mpv_error_string(result)));
        return;
    }
    loadInFlight_ = true;
    std::fprintf(stderr, "loadfile submitted: %s (%s)\n", path.constData(), options.constData());
}

void Player::setPaused(bool paused)
{
    if (state_ != State::Loaded)
        return;
    const char *args[] = {"set", "pause", paused ? "yes" : "no", nullptr};
    submitControl(Pause, args);
}

void Player::seek(double seconds)
{
    if (state_ != State::Loaded || !std::isfinite(seconds) || seconds < 0 || closing_)
        return;
    const QByteArray target = QByteArray::number(seconds, 'f', 3);
    const char *args[] = {"seek", target.constData(), "absolute+exact", nullptr};
    std::fprintf(stderr, "seek submitted: %s s\n", target.constData());
    submitControl(Seek, args);
}

void Player::setVolume(double volume)
{
    if (!std::isfinite(volume))
        return;
    const QByteArray value = QByteArray::number(std::clamp(volume, 0.0, 100.0), 'f', 1);
    const char *args[] = {"set", "volume", value.constData(), nullptr};
    submitControl(Volume, args);
}

void Player::submitControl(uint64_t request, const char **args)
{
    if (!handle_ || closing_)
        return;
    const int result = mpv_command_async(handle_, request, args);
    if (check(controlName(request), result)) {
        ++controlsInFlight_;
    } else {
        emit commandError(QStringLiteral("%1: %2").arg(controlName(request), mpv_error_string(result)));
    }
}

void Player::changeState(State state, const QString &error)
{
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
    if (event.reply_userdata == Load) {
        loadInFlight_ = false;
        if (!loadPending_) {
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
        --controlsInFlight_;
        if (!check(controlName(event.reply_userdata), event.error) && !loadPending_)
            emit commandError(QStringLiteral("%1: %2").arg(controlName(event.reply_userdata), mpv_error_string(event.error)));
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
        std::fprintf(stderr, "volume: %.1f\n", volume);
        emit volumeChanged(volume);
        return;
    }
    if (state_ != State::Loaded || event.reply_userdata != static_cast<uint64_t>(selectedEntry_))
        return;
    if (name == "duration") {
        const double seconds = *static_cast<double *>(property.data);
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
        std::fprintf(stderr, "audio output: %lld Hz, %lld channels\n",
                     static_cast<long long>(mapInteger(node, "samplerate")),
                     static_cast<long long>(mapInteger(node, "channel-count")));
    }
}
