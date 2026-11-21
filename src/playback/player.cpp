#include "player.h"

#include <QByteArray>
#include <QDir>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

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
    {"seekable", MPV_FORMAT_FLAG}, {"estimated-vf-fps", MPV_FORMAT_DOUBLE},
    {"track-list", MPV_FORMAT_NODE}, {"aid", MPV_FORMAT_NODE}, {"sid", MPV_FORMAT_NODE},
    {"ab-loop-a", MPV_FORMAT_NODE}, {"ab-loop-b", MPV_FORMAT_NODE},
    {"video-params", MPV_FORMAT_NODE}, {"audio-params", MPV_FORMAT_NODE},
    {"video-codec", MPV_FORMAT_NODE}, {"audio-codec-name", MPV_FORMAT_NODE},
    {"file-format", MPV_FORMAT_NODE}, {"container-fps", MPV_FORMAT_NODE}, {"chapter-list", MPV_FORMAT_NODE}
};
constexpr const char *globalProperties[] = {"speed", "audio-pitch-correction", "sub-scale", "sub-visibility", "audio-delay", "sub-delay", "video-rotate"};
int normalizedRotation(int degrees) { return (degrees % 360 + 360) % 360; }
bool globalProperty(const QString &name)
{
    return std::ranges::any_of(globalProperties, [&name](const char *key) { return name == key; });
}
QByteArray propertyText(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::Bool) return value.toBool() ? "yes" : "no";
    if (value.metaType().id() == QMetaType::Double) return QByteArray::number(value.toDouble(), 'g', 15);
    return value.toString().toUtf8();
}
bool samePropertyValue(const QVariant &left, const QVariant &right)
{
    bool aValid = false, bValid = false; const double a = left.toDouble(&aValid), b = right.toDouble(&bValid);
    // Some mpv options are floats. Allow their rounding while distinguishing
    // adjacent millisecond delay values even at the ends of the range.
    if (aValid && bValid) return std::abs(a-b) <= std::max(1e-6, std::numeric_limits<float>::epsilon() * std::max(std::abs(a), std::abs(b)));
    return left == right;
}
} // namespace

Player::Player(QObject *parent) : QObject(parent)
{
    seekTimer_.setSingleShot(true);
    seekTimer_.setInterval(35);
    connect(&seekTimer_, &QTimer::timeout, this, &Player::flushSeek);
    seekTimeout_.setSingleShot(true);
    seekTimeout_.setInterval(5000);
    connect(&seekTimeout_, &QTimer::timeout, this, [this] {
        std::fputs("seek confirmation timed out\n", stderr);
        settleSeek(std::nullopt);
    });
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
        {"input-default-bindings", "no"}, {"idle", "yes"}, {"sub-auto", "no"},
        {"keepaspect", "yes"}, {"video-unscaled", "no"}, {"panscan", "0"},
        {"video-align-x", "0"}, {"video-align-y", "0"},
        {"video-margin-ratio-left", "0"}, {"video-margin-ratio-right", "0"},
        {"video-margin-ratio-top", "0"}, {"video-margin-ratio-bottom", "0"},
        {"video-zoom", "0"}, {"video-pan-x", "0"}, {"video-pan-y", "0"}, {"video-rotate", "0"}
    };
    for (const auto &option : options) {
        if (!check(option[0], mpv_set_option_string(handle_, option[0], option[1])))
            return false;
    }
    if (!check("mpv_initialize", mpv_initialize(handle_))
        || !check("observe volume", mpv_observe_property(handle_, 0, "volume", MPV_FORMAT_DOUBLE))
        || !check("observe mute", mpv_observe_property(handle_, 0, "mute", MPV_FORMAT_FLAG)))
        return false;

    for (const auto *name : globalProperties)
        mpv_observe_property(handle_, 0, name, MPV_FORMAT_NODE);
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
    if (closing_ || rendererReady_ == ready) return;
    rendererReady_ = ready;
    if (ready) { submitPendingLoad(); return; }
    ++renderEpoch_;
    if (state_ != State::Loaded && state_ != State::Loading) return;
    if (!recovering_) {
        recovery_ = {}; recovery_.position = positionTarget();
        for (const auto &name : {"ab-loop-a", "ab-loop-b"})
            recovery_.values[name] = propertyTarget(name);
        for (const auto &track : tracks_) {
            if (track.external && !track.externalPath.isEmpty()) recovery_.external.append(track.externalPath);
            if (track.type == "audio" && track.id == propertyTarget("aid").toLongLong()) recovery_.audio = track.fingerprint();
            if (track.type == "sub" && track.id == propertyTarget("sid").toLongLong()) recovery_.subtitle = track.fingerprint();
        }
        if (propertyTarget("sid").toString() == "no") recovery_.subtitle = "no";
        for (const auto &path : initialSubtitles_) if (!recovery_.external.contains(path)) recovery_.external.append(path);
        for (const auto &request : requests_) if (request.command == Command::Subtitle && !recovery_.external.contains(request.subject)) recovery_.external.append(request.subject);
    }
    recovering_ = true; lastLoadWasRecovery_ = true; restoreStage_ = 0;
    position_ = recovery_.position;
    queueLoad(true);
}

void Player::openFile(const QString &path, quint64 itemId, const QStringList &subtitles, bool automatic)
{
    if (closing_ || !handle_ || path.isEmpty())
        return;
    recovering_ = lastLoadWasRecovery_ = false;
    resumeAllowed_=!skipResumeNext_; skipResumeNext_=false; trackPreferencesPending_=true; automaticOpen_=automatic;
    initialSubtitles_.clear();
    const QFileInfo file(path);
    const auto stem = file.completeBaseName();
    const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})?$"));
    for (const auto &name : file.dir().entryList(QDir::Files, QDir::Name)) {
        const QFileInfo sub(file.dir().filePath(name)); const auto base = sub.completeBaseName();
        if (!QStringList{"srt","ass","ssa","vtt"}.contains(sub.suffix().toLower())) continue;
        if (base.compare(stem, Qt::CaseInsensitive) == 0 || (base.startsWith(stem + '.', Qt::CaseInsensitive) && language.match(base.mid(stem.size() + 1)).hasMatch()))
            initialSubtitles_.append(sub.absoluteFilePath());
    }
    initialSubtitles_.append(subtitles); initialSubtitles_.removeDuplicates();
    pauseTarget_ = false;
    startHeld_ = false;
    emit fileOpening();
    path_ = path;
    itemId_ = itemId;
    resetLoopsPending_ = true;
    position_ = 0;
    paused_ = false;
    queueLoad();
}

void Player::queueLoad(bool recovery)
{
    if (!recovery) {
        ++generation_;
        completedPlayback_.reset();
        undoPosition_.reset();
    }
    engineLoaded_ = false;
    resetFileDetails();
    clearSeek();
    seekable_ = false; duration_ = fps_ = sampleRate_ = 0;
    emit audioAvailableChanged(false);
    if (observationId_) mpv_unobserve_property(handle_, observationId_);
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
    if (recovering_ && engineLoaded_) { restoreStep(); return; }
    if(loadPending_ && !recovering_ && trackPreferencesPending_) {
        trackPreferencesPending_=false;
        issueProperty("alang",audioLanguages_,Command::ResetLoop); issueProperty("slang",subtitleLanguages_,Command::ResetLoop);
        issueProperty("aid","auto",Command::ResetLoop); issueProperty("sid","auto",Command::ResetLoop); return;
    }
    if (resetLoopsPending_) {
        resetLoopsPending_ = false;
        const char *a[] = {"set", "ab-loop-a", "no", nullptr};
        const char *b[] = {"set", "ab-loop-b", "no", nullptr};
        submitControl(Command::ResetLoop, a); submitControl(Command::ResetLoop, b);
        return;
    }
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
    const QByteArray options = QByteArray("pause=") + ((recovering_ || startHeld_ || pauseTarget_) ? "yes" : "no")
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
    if (state_ != State::Loaded && !recovering_) return;
    pauseTarget_ = paused;
    if (recovering_) return;
    const char *args[] = {"set", "pause", paused ? "yes" : "no", nullptr};
    submitControl(Command::Pause, args);
}

void Player::togglePaused()
{
    if (state_ != State::Loaded && !recovering_) return;
    if (startHeld_) emit startPlaybackRequested();
    else setPaused(!pauseTarget_);
}

void Player::stop(bool clearFile)
{
    if (!handle_ || closing_ || path_.isEmpty())
        return;
    emit stopping();
    ++generation_;
    undoPosition_.reset();
    recovering_ = false; engineLoaded_ = false; lastLoadWasRecovery_ = false; startHeld_ = false;
    resetFileDetails();
    clearSeek();
    if (observationId_) mpv_unobserve_property(handle_, observationId_);
    selectedEntry_ = 0;
    loadPending_ = false; stopPending_ = true;
    resetLoopsPending_ = true;
    position_ = 0; paused_ = false; seekable_ = false;
    if (clearFile) { path_.clear(); itemId_ = 0; }
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
    return std::isfinite(duration_) && duration_ > 0 && interval > 0 ? std::max(0.0, duration_ - interval) : 0;
}

bool Player::canSeek() const
{
    return !closing_ && !recovering_ && state_ == State::Loaded && seekable_ && lastPosition() > 0;
}

double Player::positionTarget() const
{
    if (pendingSeek_) return pendingSeek_->target;
    return submittedSeek_ ? submittedSeek_->target : position_;
}

void Player::clearSeek()
{
    const bool wasSeeking = seekInProgress();
    seekTimer_.stop(); seekTimeout_.stop();
    pendingSeek_.reset(); submittedSeek_.reset(); relativeSeekOrigin_.reset();
    initialSeek_.reset();
    seekRequest_ = 0;
    if (wasSeeking) emit seekActivityChanged();
}

void Player::seek(double seconds)
{
    seekFrom(seconds, position_);
}

void Player::seekFrom(double seconds, double origin)
{
    if (!canSeek() || !std::isfinite(seconds) || !std::isfinite(origin)) return;
    relativeSeekOrigin_.reset();
    queueSeek({seconds, origin});
}

void Player::queueSeek(Seek seek)
{
    const bool wasSeeking = seekInProgress();
    const bool user = seek.origin.has_value() || seek.undo;
    seek.target = std::clamp(seek.target, 0.0, lastPosition());
    pendingSeek_ = seek;
    if (user) {
        const auto a = propertyTarget("ab-loop-a"), b = propertyTarget("ab-loop-b");
        if (a.isValid() && b.isValid() && a.toString() != "no" && b.toString() != "no"
            && (seek.target < a.toDouble() || seek.target > b.toDouble())) clearLoop();
        if (startHeld_) initialSeek_ = InitialSeek{playbackIdentity(), StartPosition::User};
    }
    if (!wasSeeking) emit seekActivityChanged();
    if (user) emit seekRequested(seek.target);
    if (pendingSeek_ && !submittedSeek_ && !seekTimer_.isActive()) seekTimer_.start();
}

bool Player::seekToStart(double seconds, StartPosition reason)
{
    if (!canSeek() || !std::isfinite(seconds) || seconds < 0 || seconds > lastPosition()) return false;
    initialSeek_ = InitialSeek{playbackIdentity(), reason};
    queueSeek({seconds, std::nullopt});
    return true;
}

void Player::cancelStartSeek()
{
    if (initialSeek_) clearSeek();
}

bool Player::naturalProgress() const
{
    return state_ == State::Loaded && engineLoaded_ && !recovering_ && !startHeld_
        && !paused_ && !pauseTarget_ && !seekInProgress() && !framePending_ && !loopActive();
}

bool Player::completeOutro(PlaybackIdentity playback)
{
    if (closing_ || completedPlayback_ || !playback.entry || playback != playbackIdentity() || !naturalProgress()) return false;
    const char *args[] = {"stop", "keep-playlist", nullptr};
    // The load barrier waits for this request before replacing the entry.
    if (!submitControl(Command::FinishOutro, args)) return false;
    finishCurrentPlayback(playback, CompletionReason::SkippedOutro);
    return true;
}

void Player::seekRelative(double seconds, bool continuing)
{
    if (!canSeek() || !std::isfinite(seconds)) return;
    if (!continuing || !relativeSeekOrigin_) relativeSeekOrigin_ = position_;
    queueSeek({positionTarget() + seconds, relativeSeekOrigin_});
}

void Player::undoSeek()
{
    if (!canUndoSeek()) return;
    relativeSeekOrigin_.reset();
    queueSeek({*undoPosition_, std::nullopt, true});
}

void Player::flushSeek()
{
    if (!pendingSeek_ || submittedSeek_ || state_ != State::Loaded) return;
    submittedSeek_ = std::exchange(pendingSeek_, std::nullopt);
    const QByteArray target = QByteArray::number(submittedSeek_->target, 'f', 6);
    const char *args[] = {"seek", target.constData(), "absolute+exact", nullptr};
    std::fprintf(stderr, "seek submitted: %s s\n", target.constData());
    seekStarted_ = seekRestarted_ = false;
    seekRequest_ = nextRequest_;
    seekTimeout_.start();
    if (!submitControl(Command::Seek, args)) settleSeek(std::nullopt);
}

void Player::finishSeek()
{
    if (!submittedSeek_ || seekRequest_ || !seekRestarted_) return;
    // An observed time-pos can lag the restart or still describe the old frame.
    seekRequest_ = nextRequest_;
    if (!readProperty("time-pos", Command::ReadSeekPosition)) settleSeek(std::nullopt);
}

void Player::settleSeek(std::optional<double> confirmedPosition)
{
    const auto seek = std::exchange(submittedSeek_, std::nullopt);
    if (!seek) return;
    seekTimeout_.stop();
    seekRequest_ = 0;
    const auto playback = playbackIdentity();
    if (confirmedPosition) {
        position_ = *confirmedPosition;
        if (seek->undo) undoPosition_.reset();
        else if (seek->origin && std::abs(seek->target - *seek->origin) > 1e-6 && std::abs(position_ - *seek->origin) > 1e-6)
            undoPosition_ = seek->origin;
    }
    emit positionChanged(position_);
    if (playback != playbackIdentity()) return;
    if (pendingSeek_) { seekTimer_.start(); return; }
    const auto initial = std::exchange(initialSeek_, std::nullopt);
    if (initial && initial->playback == playback)
        emit startPositionResolved(playback, initial->reason, confirmedPosition.has_value());
    emit seekActivityChanged();
    emit seekSettled(position_);
}

void Player::setVolume(double volume)
{
    setAudioPreferences(volume, false);
}

void Player::setAudioPreferences(double volume, bool muted)
{
    if (!std::isfinite(volume))
        return;
    if (muteTarget_ != muted) setMuted(muted);
    // Keep fractional wheel input between events, including while a previous
    // slider or wheel request is still waiting for its asynchronous reply.
    const double target = std::clamp(volume, 0.0, 100.0);
    if (target == volumeTarget_ && target == reportedVolume_) return;
    const double previous = volumeTarget_;
    volumeTarget_ = target;
    applyVolume();
    if (previous != volumeTarget_) emit preferenceTargetChanged();
}

void Player::applyVolume()
{
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
    const bool previous = muteTarget_;
    muteTarget_ = muted;
    if (!muteInFlight_) {
        const char *args[] = {"set", "mute", muted ? "yes" : "no", nullptr};
        if (submitControl(Command::Mute, args)) muteInFlight_ = muted;
        else { muteTarget_ = reportedMute_; emit mutedChanged(reportedMute_); }
    }
    if (previous != muteTarget_) emit preferenceTargetChanged();
}

void Player::addSubtitle(const QString &path, bool select)
{
    if (state_ != State::Loaded) return;
    const auto key = mediaPathKey(path);
    for (const auto &track : tracks_) if (track.external && mediaPathKey(track.externalPath) == key) { if(select) selectTrack("sub", track.id, generation_); return; }
    for (const auto &request : requests_) if (request.command == Command::Subtitle && mediaPathKey(request.subject) == key) return;
    const QByteArray filename = QFileInfo(path).absoluteFilePath().toUtf8();
    const char *args[] = {"sub-add", filename.constData(), select?"select":"auto", nullptr};
    submitControl(Command::Subtitle, args, QString::fromUtf8(filename));
}

bool Player::submitControl(Command command, const char **args, const QString &subject, const QVariant &value)
{
    if (!handle_ || closing_) return false;
    const uint64_t request = nextRequest_++;
    const int result = mpv_command_async(handle_, request, args);
    if (check(args[0], result)) {
        Request pending{command, generation_, renderEpoch_, command == Command::Load ? 0 : selectedEntry_, subject, value};
        if (command == Command::Screenshot) {
            pending.transformVersion = videoTransformVersion_;
            pending.rotation = videoRotation_;
        }
        requests_.insert(request, pending);
        emit requestSubmitted(request, generation_, renderEpoch_, QString::fromUtf8(args[0]) + ' ' + subject);
        return true;
    }
    emit commandError(QStringLiteral("%1: %2").arg(args[0], mpv_error_string(result))); return false;
}

void Player::changeState(State state, const QString &error)
{
    if (state == State::Error) recovering_ = false;
    state_ = state;
    if (state != State::Loaded) {
        clearSeek();
        properties_.remove("chapter-list");
        updateChapters();
    }
    const auto generation = generation_, item = itemId_;
    emit mediaChanged(state, path_, error);
    emit sessionChanged(generation, item, state, error);
}

void Player::finishCurrentPlayback(PlaybackIdentity playback, CompletionReason reason)
{
    if (closing_ || completedPlayback_ || !playback.entry || playback != playbackIdentity()
        || (state_ != State::Loaded && state_ != State::Loading)) return;

    completedPlayback_ = playback;
    engineLoaded_ = false;
    recovering_ = false;
    changeState(State::Ended);
    if (ownsCompletion(playback)) emit playbackCompleted(playback, reason);
}

bool Player::ownsCompletion(const PlaybackIdentity &playback) const
{
    // A renderer rebuild after the end does not change the accepted completion.
    return !closing_ && state_ == State::Ended && completedPlayback_ == playback
        && generation_ == playback.generation && itemId_ == playback.itemId && selectedEntry_ == playback.entry;
}

void Player::drainEvents()
{
    while (!closing_) {
        const mpv_event &event = *mpv_wait_event(handle_, 0);
        switch (event.event_id) {
        case MPV_EVENT_NONE:
            return;
        case MPV_EVENT_COMMAND_REPLY:
        case MPV_EVENT_GET_PROPERTY_REPLY:
            handleReply(event);
            break;
        case MPV_EVENT_SEEK:
            if (submittedSeek_ && eventEntry_ == selectedEntry_) {
                seekStarted_ = true;
                seekRestarted_ = false;
            }
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            if (eventEntry_ == selectedEntry_) {
                if (submittedSeek_ && seekStarted_) { seekRestarted_ = true; finishSeek(); }
                if (recovering_ && engineLoaded_) { restoreRestarted_ = true; restoreStep(); }
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
    const bool global = request.command == Command::Property && globalProperty(request.subject);
    const bool current = global || (request.generation == generation_ && request.epoch == renderEpoch_ && (!request.entry || request.entry == selectedEntry_));
    emit requestFinished(event.reply_userdata, current, event.error >= 0);
    if (request.command == Command::ReadSeekPosition) {
        if (current && event.reply_userdata == seekRequest_) {
            std::optional<double> position;
            if (event.error >= 0) {
                const auto &property = *static_cast<mpv_event_property *>(event.data);
                if (property.data && property.format == MPV_FORMAT_NODE) {
                    const auto &node = *static_cast<mpv_node *>(property.data);
                    if (node.format == MPV_FORMAT_DOUBLE && std::isfinite(node.u.double_)) position = node.u.double_;
                }
            }
            settleSeek(position);
        }
        submitPendingLoad(); return;
    }
    if (request.command == Command::ReadProperty) {
        if (current && event.error >= 0) {
            const auto &property = *static_cast<mpv_event_property *>(event.data);
            if (property.data && property.format == MPV_FORMAT_NODE) {
                const auto value = nodeValue(*static_cast<mpv_node *>(property.data)); applyProperty(request.subject,value);
                if (request.subject == "pause" && framePending_ && value.toBool()) {
                    framePending_ = false;
                    const char *args[] = {"frame-step","1","mute",nullptr}; submitControl(Command::Frame,args);
                }
            }
        }
        submitPendingLoad(); return;
    }
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
        if (request.command == Command::RestoreSeek && current && event.error < 0) restoreRestarted_ = true;
        if (request.command == Command::RestoreProperty && current && event.error < 0
            && samePropertyValue(targets_.value(request.subject), request.value)) {
            targets_.remove(request.subject);
            if (globalProperty(request.subject)) emit preferenceTargetChanged();
        }
        if (request.command == Command::FramePause && current) {
            if (event.error >= 0) readProperty("pause"); else framePending_ = false;
        }
        if (request.command == Command::Seek && current && event.reply_userdata == seekRequest_) {
            seekRequest_ = 0;
            if (event.error >= 0) finishSeek();
            else settleSeek(std::nullopt);
        }
        if (request.command == Command::Property) {
            propertyInFlight_.remove(request.subject);
            if (current) {
                if (targets_.contains(request.subject) && !samePropertyValue(targets_[request.subject],request.value)) setMediaProperty(request.subject, targets_[request.subject]);
                else if (event.error < 0) {
                    targets_.remove(request.subject);
                    if (request.subject == "video-rotate") invalidateVideoOutput(propertyTarget("video-rotate").toInt());
                    if (global) emit preferenceTargetChanged();
                    emit detailsChanged();
                }
                else if (samePropertyValue(properties_.value(request.subject),request.value)) targets_.remove(request.subject);
            }
            if (request.subject == "aid" && !propertyInFlight_.contains("aid") && targets_.contains("video-rotate"))
                setMediaProperty("video-rotate", targets_.value("video-rotate"));
            if (request.subject == "video-rotate") confirmVideoOutput();
        }
        if (request.command == Command::Subtitle && current && event.error >= 0) {
            readProperty("track-list"); readProperty("sid"); emit subtitleLoaded(request.subject);
        }
        if (request.command == Command::Screenshot && current && event.error >= 0 && videoTransformReady()
            && request.transformVersion == videoTransformVersion_) {
            const auto &node = static_cast<mpv_event_command *>(event.data)->result;
            const int width = int(mapInteger(node,"w")), height = int(mapInteger(node,"h")), stride = int(mapInteger(node,"stride"));
            const mpv_byte_array *bytes = nullptr;
            if (node.format == MPV_FORMAT_NODE_MAP) for (int i = 0; i < node.u.list->num; ++i)
                if (std::strcmp(node.u.list->keys[i],"data") == 0 && node.u.list->values[i].format == MPV_FORMAT_BYTE_ARRAY) bytes = node.u.list->values[i].u.ba;
            if (bytes && width > 0 && height > 0 && stride > 0 && size_t(stride) >= size_t(width) * 4 && bytes->size >= size_t(stride) * height) {
                QImage image(width,height,QImage::Format_RGBA8888);
                if (image.isNull()) { emit commandError(QStringLiteral("无法分配截图内存")); submitPendingLoad(); return; }
                const auto *source = static_cast<const uchar *>(bytes->data);
                for (int y = 0; y < height; ++y) std::memcpy(image.scanLine(y),source + qptrdiff(y) * stride,size_t(width)*4);
                emit sourceImageReady(image, request.generation, request.epoch, request.transformVersion,
                                      request.rotation, request.subject, request.value.toDouble());
            } else emit commandError(QStringLiteral("截图数据无效"));
        }
        if (request.command == Command::Volume) {
            const bool newerTarget = volumeTarget_ != volumeInFlight_.value();
            volumeInFlight_.reset();
            if (newerTarget)
                applyVolume();
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
    if (eventEntry_ != selectedEntry_ || !selectedEntry_ || completedPlayback_) return;
    if (eventState_ == State::Ended) {
        finishCurrentPlayback(playbackIdentity(), CompletionReason::NaturalEof);
        return;
    }
    if (eventState_ == State::Loaded) {
        if (engineLoaded_) return;
        engineLoaded_ = true; observationId_ = nextObservation_++;
        for (const auto &property : fileProperties) mpv_observe_property(handle_, observationId_, property.name, property.format);
        if (recovering_) { restoreStep(); return; }
        changeState(State::Loaded);
        for (const auto &path : initialSubtitles_) addSubtitle(path,false);
        return;
    }
    if (eventState_ != state_) { engineLoaded_ = false; recovering_ = false; changeState(eventState_, eventError_); }
}

void Player::propertyChanged(const mpv_event &event)
{
    const auto &property = *static_cast<mpv_event_property *>(event.data);
    const QByteArrayView name(property.name);
    if (name == "chapter-list") {
        if (engineLoaded_ && event.reply_userdata == observationId_) {
            const auto value = property.format == MPV_FORMAT_NODE && property.data ? nodeValue(*static_cast<mpv_node *>(property.data)) : QVariant();
            properties_["chapter-list"] = value;
            updateChapters();
        }
        return;
    }
    if (property.format == MPV_FORMAT_NONE || !property.data) {
        if (engineLoaded_ && event.reply_userdata == observationId_ && name == "video-out-params") {
            reportedVideoSize_ = {};
            if (videoOutputReady_) {
                invalidateVideoOutput(propertyTarget("video-rotate").toInt());
                videoSize_ = {};
                emit videoSizeChanged(videoSize_);
            }
        }
        return;
    }
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
    if (event.reply_userdata == 0 && property.format == MPV_FORMAT_NODE && globalProperty(QString::fromUtf8(property.name))) {
        applyProperty(QString::fromUtf8(property.name), nodeValue(*static_cast<mpv_node *>(property.data))); return;
    }
    if (!engineLoaded_ || event.reply_userdata != observationId_) return;
    if (property.format == MPV_FORMAT_NODE && name != "video-out-params" && name != "audio-out-params") {
        applyProperty(QString::fromUtf8(property.name), nodeValue(*static_cast<mpv_node *>(property.data))); return;
    }
    if (name == "seekable") {
        seekable_ = *static_cast<int *>(property.data) != 0;
        emit seekableChanged(seekable_);
    } else if (name == "estimated-vf-fps") {
        const bool wasUnavailable=fps_<=0;
        fps_ = *static_cast<double *>(property.data);
        if(wasUnavailable && fps_>0) emit detailsChanged();
    } else if (name == "duration") {
        const double seconds = *static_cast<double *>(property.data);
        duration_ = seconds;
        std::fprintf(stderr, "duration: %.3f s\n", seconds);
        updateChapters();
        emit durationChanged(seconds);
    } else if (name == "time-pos") {
        // Until the seek restarts playback, mpv may report only its requested timestamp.
        if (submittedSeek_) return;
        const double before = position_;
        position_ = *static_cast<double *>(property.data);
        const auto playback = playbackIdentity();
        const bool advancing = naturalProgress();
        emit positionChanged(position_);
        if (playback != playbackIdentity()) return;
        if (advancing && position_ > before) emit playbackAdvanced(playback, before, position_);
    } else if (name == "pause") {
        paused_ = *static_cast<int *>(property.data) != 0;
        if (!recovering_) pauseTarget_ = paused_;
        std::fprintf(stderr, "pause: %s\n", paused_ ? "yes" : "no");
        emit pausedChanged(paused_);
    } else if (name == "video-out-params") {
        const auto &node = *static_cast<mpv_node *>(property.data);
        // dw/dh include pixel aspect ratio; rotate is the remaining VO rotation.
        QSize size(int(mapInteger(node, "dw")), int(mapInteger(node, "dh")));
        reportedRotation_ = normalizedRotation(int(mapInteger(node, "rotate")));
        if (reportedRotation_ % 180 == 90)
            size.transpose();
        reportedVideoSize_ = size;
        confirmVideoOutput();
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

void Player::resetFileDetails()
{
    framePending_ = false;
    tracks_.clear(); videoSize_ = {}; videoRotation_ = 0; properties_.remove("track-list");
    reportedVideoSize_ = {};
    rotationBase_.reset();
    expectedRotation_.reset();
    videoOutputReady_ = false;
    for (const auto &name : {"aid","sid","ab-loop-a","ab-loop-b","video-params","audio-params","video-codec","audio-codec-name","file-format","container-fps"}) {
        properties_.remove(name); targets_.remove(name);
    }
    emit detailsChanged();
}

void Player::invalidateVideoOutput(int target)
{
    ++videoTransformVersion_;
    if (rotationBase_) expectedRotation_ = normalizedRotation(*rotationBase_ + target);
    if (!videoOutputReady_) return;
    videoOutputReady_ = false;
    emit videoTransformChanged();
}

void Player::confirmVideoOutput()
{
    if (reportedVideoSize_.isEmpty() || recovering_ || state_ != State::Loaded
        || propertyInFlight_.contains("video-rotate")) return;
    const int target = propertyTarget("video-rotate").toInt();
    if (target != mediaProperty("video-rotate").toInt() || (expectedRotation_ && *expectedRotation_ != reportedRotation_)) return;

    // The output angle includes the container orientation and the user adjustment.
    expectedRotation_.reset();
    rotationBase_ = normalizedRotation(reportedRotation_ - target);
    if (videoOutputReady_ && videoSize_ == reportedVideoSize_ && videoRotation_ == reportedRotation_) return;
    videoOutputReady_ = true;
    ++videoTransformVersion_;
    videoSize_ = reportedVideoSize_;
    videoRotation_ = reportedRotation_;
    std::fprintf(stderr, "video display size: %dx%d (%d degrees)\n", videoSize_.width(), videoSize_.height(), videoRotation_);
    emit videoSizeChanged(videoSize_);
    emit videoTransformChanged();
}

bool Player::issueProperty(const QString &name, const QVariant &value, Command command)
{
    const auto key = name.toUtf8(), text = propertyText(value);
    const char *args[] = {"set",key.constData(),text.constData(),nullptr};
    return submitControl(command,args,name,value);
}

void Player::setMediaProperty(const QString &name, const QVariant &value)
{
    const bool global = globalProperty(name);
    if (!handle_ || closing_ || (!global && state_ != State::Loaded)) return;
    QVariant target = value;
    if (name == "speed" || name == "sub-scale" || name == "audio-delay" || name == "sub-delay") {
        bool valid = false; double number = value.toDouble(&valid);
        if (!valid || !std::isfinite(number)) return;
        if (name == "speed") number = std::clamp(number,.25,3.0);
        if (name == "sub-scale") number = std::clamp(number,.5,2.0);
        if (name == "audio-delay" || name == "sub-delay") number = std::clamp(number, -3600.0, 3600.0);
        target = number;
    } else if (name == "video-rotate") {
        bool valid = false;
        const int degrees = value.toInt(&valid);
        if (!valid || degrees % 90 != 0) return;
        target = normalizedRotation(degrees);
    } else if (name != "aid" && name != "sid" && name != "ab-loop-a" && name != "ab-loop-b"
        && name != "sub-visibility" && name != "audio-pitch-correction") return;
    const auto previous = propertyTarget(name);
    if (name == "video-rotate" && previous.toInt() == target.toInt() && !targets_.contains(name)) return;
    targets_[name] = target;
    if (name == "video-rotate" && previous.toInt() != target.toInt()) invalidateVideoOutput(target.toInt());
    // An audio track switch can reposition the demuxer. Let it settle before mpv refreshes a paused frame.
    const bool switchingAudio = name == "video-rotate" && propertyInFlight_.contains("aid");
    if (!propertyInFlight_.contains(name) && !recovering_ && !switchingAudio) {
        if (issueProperty(name,target)) propertyInFlight_.insert(name);
        else {
            targets_.remove(name);
            if (name == "video-rotate") {
                invalidateVideoOutput(propertyTarget(name).toInt());
                confirmVideoOutput();
            }
            emit detailsChanged();
        }
    }
    if (global && previous != propertyTarget(name)) emit preferenceTargetChanged();
}
void Player::adjustMediaProperty(const QString &name, double delta) { setMediaProperty(name,propertyTarget(name).toDouble()+delta); }
void Player::setTrackLanguages(const QString &audio,const QString &subtitles) { audioLanguages_=audio; subtitleLanguages_=subtitles; }
void Player::selectTrack(const QString &type, qlonglong id, quint64 generation)
{
    if (state_ != State::Loaded || generation != generation_) return;
    if (type == "sub" && id == 0) { setMediaProperty("sid","no"); return; }
    for (const auto &track : tracks_) if (track.type == type && track.id == id) {
        const QString property = type == "audio" ? "aid" : "sid";
        if (propertyTarget(property).toLongLong() != id) setMediaProperty(property,id);
        return;
    }
}
void Player::nextTrack(const QString &type)
{
    QVector<qlonglong> ids; for (const auto &track : tracks_) if (track.type == type) ids.append(track.id);
    if (ids.size() < 2) return;
    const auto current = propertyTarget(type == "audio" ? "aid" : "sid").toLongLong();
    selectTrack(type,ids[(ids.indexOf(current)+1)%ids.size()],generation_);
}
void Player::frameStep()
{
    if (state_ != State::Loaded || videoSize_.isEmpty() || startHeld_) return;
    if (framePending_) return;
    clearSeek(); pauseTarget_ = true; framePending_ = true;
    if (!issueProperty("pause",true,Command::FramePause)) framePending_ = false;
}
void Player::setLoopA()
{
    if (!canSetLoopPoint()) return;
    const double position = position_;
    setMediaProperty("ab-loop-b","no"); setMediaProperty("ab-loop-a",position);
    emit notice(QStringLiteral("已设置 A 点：%1 秒").arg(position,0,'f',3));
}
void Player::setLoopB()
{
    if (!canSetLoopPoint()) return;
    const auto a = propertyTarget("ab-loop-a");
    if (!a.isValid() || a.toString() == "no") { setLoopA(); emit notice(QStringLiteral("已设置 A，请继续设置 B")); return; }
    const double position = position_;
    if (position - a.toDouble() < .5) { emit notice(QStringLiteral("B 必须比 A 晚至少 0.5 秒")); return; }
    setMediaProperty("ab-loop-b",position);
    emit notice(QStringLiteral("A–B 区间：%1 — %2 秒").arg(a.toDouble(),0,'f',3).arg(position,0,'f',3));
}
void Player::clearLoop() { setMediaProperty("ab-loop-a","no"); setMediaProperty("ab-loop-b","no"); }
bool Player::loopActive() const
{
    const auto a = propertyTarget("ab-loop-a"), b = propertyTarget("ab-loop-b");
    return a.isValid() && b.isValid() && a.toString() != "no" && b.toString() != "no" && b.toDouble()-a.toDouble() >= .5;
}
void Player::captureSource()
{
    if (!videoTransformReady()) return;
    const char *args[] = {"screenshot-raw","video","rgba",nullptr}; submitControl(Command::Screenshot,args,path_,position_);
}
bool Player::readProperty(const QString &name, Command command)
{
    const quint64 id = nextRequest_++; const auto key = name.toUtf8();
    if (mpv_get_property_async(handle_,id,key.constData(),MPV_FORMAT_NODE) < 0) return false;
    requests_.insert(id,{command,generation_,renderEpoch_,selectedEntry_,name,{}});
    emit requestSubmitted(id,generation_,renderEpoch_,"get " + name);
    return true;
}
void Player::applyProperty(const QString &name, const QVariant &value)
{
    // Track selection uses mpv's "no" spelling in commands; NODE feedback can
    // represent that same value as false. Keep one representation in snapshots.
    const QVariant actual = (name == "aid" || name == "sid") && value.metaType().id() == QMetaType::Bool && !value.toBool() ? QVariant("no") : value;
    const bool global = globalProperty(name);
    const auto previous = global ? propertyTarget(name) : QVariant();
    properties_[name] = actual;
    if (!propertyInFlight_.contains(name) && samePropertyValue(targets_.value(name),actual)) targets_.remove(name);
    if (name == "track-list") updateTracks();
    if (name == "video-rotate") {
        if (previous.toInt() != propertyTarget(name).toInt()) invalidateVideoOutput(propertyTarget(name).toInt());
        confirmVideoOutput();
    }
    if (global && previous != propertyTarget(name)) emit preferenceTargetChanged();
    emit detailsChanged();
}
void Player::updateTracks()
{
    tracks_.clear(); QHash<QString,int> ordinals;
    for (const auto &value : properties_.value("track-list").toList()) {
        const auto item = value.toMap(); MediaTrack track;
        track.id = item.value("id").toLongLong(); track.type = item.value("type").toString();
        track.ordinal = ordinals[track.type]++; track.language = item.value("lang").toString();
        track.title = item.value("title").toString(); track.codec = item.value("codec").toString();
        track.external = item.value("external").toBool(); track.externalPath = item.value("external-filename").toString();
        track.selected = item.value("selected").toBool(); tracks_.append(track);
    }
}

void Player::updateChapters()
{
    QVector<MediaChapter> chapters;
    const auto entries = properties_.value("chapter-list").toList();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const auto entry = entries[i].toMap();
        const auto time = entry.value("time");
        bool numeric = false;
        const double start = time.toDouble(&numeric);
        if (!numeric || time.metaType().id() == QMetaType::Bool || !std::isfinite(start) || start < 0
            || (duration_ > 0 && start >= duration_)) continue;
        const auto title = entry.value("title").toString().trimmed();
        // mpv's Matroska demuxer supplies this placeholder when no title exists.
        const bool unnamed = title.isEmpty() || title == QStringLiteral("(unnamed)");
        chapters.append({start, unnamed ? QStringLiteral("第 %1 章").arg(i + 1) : title});
    }
    std::stable_sort(chapters.begin(), chapters.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
    if (chapters_ == chapters) return;
    chapters_ = std::move(chapters);
    emit chaptersChanged();
}

qsizetype Player::currentChapter() const
{
    if (!std::isfinite(position_)) return -1;
    const auto next = std::upper_bound(chapters_.cbegin(), chapters_.cend(), position_ + 1e-6,
        [](double position, const MediaChapter &chapter) { return position < chapter.start; });
    return next - chapters_.cbegin() - 1;
}
void Player::restoreStep()
{
    if (!recovering_ || !engineLoaded_ || !rendererReady_ || !requests_.isEmpty()) return;
    std::fprintf(stderr,"restore stage=%d generation=%llu epoch=%llu restart=%d\n",restoreStage_,static_cast<unsigned long long>(generation_),static_cast<unsigned long long>(renderEpoch_),restoreRestarted_);
    switch (restoreStage_) {
    case 0:
        ++restoreStage_;
        issueProperty("ab-loop-a","no",Command::RestoreProperty); issueProperty("ab-loop-b","no",Command::RestoreProperty);
        for (const auto *name : globalProperties) {
            const auto value = propertyTarget(name);
            if (value.isValid()) issueProperty(name,value,Command::RestoreProperty);
        }
        break;
    case 1:
        ++restoreStage_;
        for (const auto &path : recovery_.external) {
            if (!QFileInfo::exists(path)) { emit notice(QStringLiteral("恢复时找不到外挂字幕：%1").arg(path)); continue; }
            const auto bytes = path.toUtf8(); const char *args[] = {"sub-add",bytes.constData(),"auto",nullptr};
            submitControl(Command::RestoreSubtitle,args,path);
        }
        if (requests_.isEmpty()) QTimer::singleShot(0,this,&Player::restoreStep);
        break;
    case 2: ++restoreStage_; readProperty("track-list"); break;
    case 3: {
        ++restoreStage_;
        for (const auto &type : {QString("audio"),QString("sub")}) {
            const auto fingerprint = type == "audio" ? recovery_.audio : recovery_.subtitle;
            const auto property = type == "audio" ? "aid" : "sid";
            bool found = fingerprint.isEmpty();
            if (fingerprint == "no") { issueProperty(property,"no",Command::RestoreProperty); found = true; }
            else for (const auto &track : tracks_) if (track.type == type && track.fingerprint() == fingerprint) {
                issueProperty(property,track.id,Command::RestoreProperty); found = true; break;
            }
            if (!found) {
                if (type == "sub") issueProperty("sid","no",Command::RestoreProperty);
                emit notice(QStringLiteral("原轨道不可用，已采用可用选择"));
            }
        }
        if (requests_.isEmpty()) QTimer::singleShot(0,this,&Player::restoreStep);
        break;
    }
    case 4: {
        ++restoreStage_; restoreRestarted_ = false;
        const auto target = QByteArray::number(recovery_.position,'f',6);
        const char *args[] = {"seek",target.constData(),"absolute+exact",nullptr};
        if (!submitControl(Command::RestoreSeek,args)) { restoreRestarted_ = true; QTimer::singleShot(0,this,&Player::restoreStep); }
        break;
    }
    case 5: {
        if (!restoreRestarted_) return;
        ++restoreStage_;
        const auto a = recovery_.values.value("ab-loop-a"), b = recovery_.values.value("ab-loop-b");
        if (a.isValid() && b.isValid() && a.toString() != "no" && b.toString() != "no"
            && b.toDouble()-a.toDouble() >= .5 && b.toDouble() <= duration_) {
            issueProperty("ab-loop-a",a,Command::RestoreProperty); issueProperty("ab-loop-b",b,Command::RestoreProperty);
        }
        if (requests_.isEmpty()) QTimer::singleShot(0,this,&Player::restoreStep);
        break;
    }
    case 6: ++restoreStage_; issueProperty("pause",pauseTarget_ || startHeld_,Command::RestoreProperty); break;
    case 7:
        ++restoreStage_;
        for (const auto &name : {"track-list","aid","sid","speed","sub-visibility","sub-scale","sub-delay","audio-delay","audio-pitch-correction","video-rotate","ab-loop-a","ab-loop-b"}) readProperty(name);
        break;
    default: {
        bool pending = false;
        for (const auto *name : globalProperties)
            if (targets_.contains(name) && !samePropertyValue(targets_[name],properties_.value(name))) { issueProperty(name,targets_[name],Command::RestoreProperty); pending = true; }
        if (pending) { restoreStage_ = 7; return; }
        recovering_ = false; changeState(State::Loaded); confirmVideoOutput(); publishFileProperties(); emit restored(generation_,renderEpoch_); break;
    }
    }
}
void Player::publishFileProperties()
{
    emit durationChanged(duration_); emit positionChanged(position_); emit pausedChanged(paused_);
    emit seekableChanged(seekable_); emit videoSizeChanged(videoSize_); emit audioAvailableChanged(sampleRate_ > 0); emit detailsChanged();
}
