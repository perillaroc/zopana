#include "playlistcontroller.h"
#include <QFileInfo>
#include <QTimer>
#include <utility>

PlaylistController::PlaylistController(Player &player, QObject *parent, std::optional<quint32> shuffleSeed)
    : QObject(parent), model_(this), player_(player),
      random_(shuffleSeed ? QRandomGenerator(*shuffleSeed) : QRandomGenerator::securelySeeded())
{
    connect(&player_, &Player::sessionChanged, this, &PlaylistController::session);
    connect(&player_, &Player::playbackCompleted, this, [this](const Player::PlaybackIdentity &playback) {
        if (completionHandler_) completionHandler_(playback);
        else advanceAfterCompletion(playback);
    });
    connect(&player_, &Player::pausedChanged, this, [this](bool paused) {
        if (player_.itemId() == model_.currentId()) model_.setStatus(model_.currentId(), paused ? QStringLiteral("已暂停") : QStringLiteral("正在播放"));
    });
    connect(&model_, &QAbstractItemModel::rowsMoved, this, [this] { emit changed(); });
}

QList<quint64> PlaylistController::addFiles(const QStringList &paths, bool replace, int row, const QStringList &subtitles)
{
    QStringList valid; int invalid = 0;
    for (const auto &path : paths) {
        const QFileInfo file(path);
        if (file.isFile() && file.isReadable() && MediaFiles::media(path)) valid.append(file.absoluteFilePath()); else ++invalid;
    }
    if (valid.isEmpty()) { emit message(QStringLiteral("没有可加入的媒体文件（%1 个无效项目）。").arg(invalid)); return {}; }
    if (replace) {
        resetAdvance();
        history_.clear();
        historyPosition_ = -1;
        bag_.clear();
        model_.clear();
    }
    const bool wasEmpty = model_.rowCount() == 0;
    const auto ids = model_.insert(valid, row);
    if (shuffleEnabled_) bag_.append(ids);
    if (!ids.isEmpty() && !subtitles.isEmpty()) model_.setSubtitles(ids.first(), subtitles);
    if (invalid || valid.size() != ids.size()) emit message(QStringLiteral("加入 %1 项；跳过 %2 个无效项目、%3 个重复项目。")
        .arg(ids.size()).arg(invalid).arg(valid.size() - ids.size()));
    if (!ids.isEmpty() && (replace || wasEmpty)) start(ids.first(), false);
    emit changed(); return ids;
}
void PlaylistController::setPlaybackOrder(RepeatMode repeatMode, bool shuffleEnabled)
{
    if (repeatMode_ == repeatMode && shuffleEnabled_ == shuffleEnabled) return;
    repeatMode_ = repeatMode;
    if (shuffleEnabled_ != shuffleEnabled) {
        shuffleEnabled_ = shuffleEnabled;
        resetShuffle();
    }
    emit playbackOrderChanged();
    emit changed();
}

void PlaylistController::cycleRepeatMode()
{
    switch (repeatMode_) {
    case RepeatMode::None: setPlaybackOrder(RepeatMode::One, shuffleEnabled_); break;
    case RepeatMode::One: setPlaybackOrder(RepeatMode::All, shuffleEnabled_); break;
    case RepeatMode::All: setPlaybackOrder(RepeatMode::None, shuffleEnabled_); break;
    }
}

void PlaylistController::setCompletionHandler(CompletionHandler handler)
{
    completionHandler_ = std::move(handler);
}

void PlaylistController::resetShuffle()
{
    history_.clear();
    historyPosition_ = -1;
    pendingHistoryEntry_ = false;
    bag_.clear();
    if (!shuffleEnabled_) return;
    refillBag();
    if (const auto current = model_.currentId()) {
        if (player_.itemId() == current && player_.state() == Player::State::Error) bag_.removeAll(current);
        else pendingHistoryEntry_ = rememberRandomItem(current) && player_.state() == Player::State::Loading;
    }
}

void PlaylistController::refillBag(const QSet<quint64> *excluded)
{
    bag_.clear();
    for (int row = 0; row < model_.rowCount(); ++row) {
        const auto id = model_.idAt(row);
        if (!excluded || !excluded->contains(id)) bag_.append(id);
    }
}

quint64 PlaylistController::nextId(const QSet<quint64> *excluded)
{
    const auto current = model_.currentId();
    if (excluded && repeatMode_ == RepeatMode::One && current)
        return excluded->contains(current) ? 0 : current;

    if (shuffleEnabled_) {
        if (excluded) bag_.removeIf([&](quint64 id) { return excluded->contains(id); });
        for (auto index = historyPosition_ + 1; index < history_.size(); ++index) {
            const auto id = history_[index];
            if (excluded && excluded->contains(id)) continue;
            historyPosition_ = index;
            bag_.removeAll(id);
            return id;
        }
        int currentIndex = -1;
        if (bag_.isEmpty()) {
            if (repeatMode_ != RepeatMode::All) return 0;
            refillBag(excluded);
            if (bag_.isEmpty()) return 0;
            if (bag_.size() > 1) currentIndex = int(bag_.indexOf(current));
        }
        const int choices = int(bag_.size()) - (currentIndex >= 0 ? 1 : 0);
        int selected = random_.bounded(choices);
        if (currentIndex >= 0 && selected >= currentIndex) ++selected;
        return bag_.takeAt(selected);
    }

    const int count = model_.rowCount();
    int row = model_.rowOf(current) + 1;
    for (int checked = 0; checked < count; ++checked, ++row) {
        if (row == count) {
            if (repeatMode_ != RepeatMode::All) break;
            row = 0;
        }
        const auto id = model_.idAt(row);
        if (!excluded || !excluded->contains(id)) return id;
    }
    return 0;
}

bool PlaylistController::canPrevious() const
{
    if (shuffleEnabled_) return previousRandomPosition() >= 0;
    return model_.rowCount() > 0 && (model_.rowOf(model_.currentId()) > 0 || repeatMode_ == RepeatMode::All);
}

int PlaylistController::previousRandomPosition() const
{
    if (historyPosition_ < 0) return -1;
    const auto current = model_.currentId();
    // A failed new selection leaves the cursor at the last opened item.
    return current && current != history_[historyPosition_] ? historyPosition_ : historyPosition_ - 1;
}

bool PlaylistController::canNext() const
{
    if (model_.rowCount() == 0) return false;
    if (repeatMode_ == RepeatMode::All) return true;
    if (shuffleEnabled_) return historyPosition_ + 1 < history_.size() || !bag_.isEmpty();
    return model_.rowOf(model_.currentId()) + 1 < model_.rowCount();
}

bool PlaylistController::rememberRandomItem(quint64 id)
{
    while (history_.size() > historyPosition_ + 1) history_.removeLast();
    bag_.removeAll(id);
    if (history_.value(historyPosition_) == id) return false;
    history_.append(id);
    if (history_.size() > shuffleHistoryLimit) history_.removeFirst();
    historyPosition_ = history_.size() - 1;
    return true;
}

void PlaylistController::resetAdvance()
{
    attemptsRemaining_ = 0;
    failed_.clear();
    skippedFailure_ = false;
}

void PlaylistController::start(quint64 id, bool automatic, bool record)
{
    const auto *item = model_.item(id); if (!item) return;
    const QString path = item->path;
    const auto subtitles = item->subtitles;
    if (!automatic) resetAdvance();
    pendingHistoryEntry_ = shuffleEnabled_ && record && rememberRandomItem(id);
    const auto before = model_.currentId();
    if (before != id) model_.setStatus(before, {});
    model_.setCurrent(id); emit selectItem(id);
    player_.openFile(path, id, subtitles, automatic); emit changed();
}
void PlaylistController::play(quint64 id) { start(id, false); }
void PlaylistController::replay()
{
    const auto current = model_.currentId();
    if (current) start(current, false, current != history_.value(historyPosition_));
    else play(model_.idAt(0));
}
void PlaylistController::next()
{
    if (const auto target = nextId()) start(target, false, target != history_.value(historyPosition_));
}

void PlaylistController::advanceAutomatically()
{
    const auto target = attemptsRemaining_ > 0 && model_.rowCount() > failed_.size() ? nextId(&failed_) : 0;
    if (!target) {
        const bool hadFailure = skippedFailure_;
        resetAdvance();
        emit message(hadFailure ? QStringLiteral("没有可继续播放的项目，已停止自动续播。") : QStringLiteral("播放列表已结束"));
        emit changed();
        return;
    }
    --attemptsRemaining_;
    start(target, true, target != history_.value(historyPosition_));
}

void PlaylistController::previous()
{
    quint64 id = 0;
    if (shuffleEnabled_) {
        if (const int previous = previousRandomPosition(); previous >= 0) {
            historyPosition_ = previous;
            id = history_[previous];
            bag_.removeAll(id);
        }
    } else {
        int row = model_.rowOf(model_.currentId()) - 1;
        if (row < 0 && repeatMode_ == RepeatMode::All) row = model_.rowCount() - 1;
        id = model_.idAt(row);
    }
    if (id) start(id, false, false);
}

void PlaylistController::advanceAfterCompletion(const Player::PlaybackIdentity &playback)
{
    if (!playback.entry || !playback.itemId || playback.itemId != model_.currentId() || playback != player_.playbackIdentity()
        || player_.state() != Player::State::Ended || player_.loopActive()) return;
    resetAdvance();
    attemptsRemaining_ = model_.rowCount();
    scheduleAdvance(playback, Player::State::Ended);
}

void PlaylistController::scheduleAdvance(Player::PlaybackIdentity playback, Player::State state)
{
    // A stop or replacement can invalidate the item before the queued advance runs.
    QTimer::singleShot(0, this, [this, playback, state] {
        if (playback != player_.playbackIdentity() || playback.itemId != model_.currentId() || player_.state() != state) return;
        if (state == Player::State::Error && repeatMode_ == RepeatMode::One) {
            resetAdvance();
            emit message(QStringLiteral("无法播放当前项目，已停止单个循环。"));
            return;
        }
        advanceAutomatically();
    });
}

void PlaylistController::session(quint64 generation, quint64 id, Player::State state, const QString &error)
{
    if (!id || id != model_.currentId() || generation != player_.generation()) return;
    const auto playback = player_.playbackIdentity();
    QString status;
    switch (state) {
    case Player::State::Loading: status = QStringLiteral("正在打开"); break;
    case Player::State::Loaded:
        status = QStringLiteral("正在播放");
        pendingHistoryEntry_ = false;
        if (!player_.lastLoadWasRecovery() && skippedFailure_) emit message(QStringLiteral("已跳过无法播放的文件"));
        resetAdvance();
        break;
    case Player::State::Idle:
        status = QStringLiteral("已停止");
        pendingHistoryEntry_ = false;
        resetAdvance();
        break;
    case Player::State::Ended:
        status = QStringLiteral("播放完毕");
        pendingHistoryEntry_ = false;
        break;
    case Player::State::Error:
        status = QStringLiteral("打开失败");
        if (std::exchange(pendingHistoryEntry_, false) && history_.value(historyPosition_) == id) {
            history_.removeAt(historyPosition_);
            --historyPosition_;
        }
        break;
    }
    if (state == Player::State::Error && !player_.lastLoadWasRecovery() && player_.automaticOpen()
        && !failed_.contains(id) && player_.state() == state) {
        failed_.insert(id);
        skippedFailure_ = true;
        scheduleAdvance(playback, state);
    }
    model_.setStatus(id, status, error);
    emit changed();
}
void PlaylistController::remove(const QList<quint64> &ids)
{
    const int currentRow = model_.rowOf(model_.currentId()); const bool current = ids.contains(model_.currentId());
    if (current) { resetAdvance(); player_.stop(true); }
    const QSet<quint64> removed(ids.cbegin(), ids.cend());
    bag_.removeIf([&](quint64 id) { return removed.contains(id); });
    for (int index = int(history_.size()) - 1; index >= 0; --index) {
        if (!removed.contains(history_[index])) continue;
        history_.removeAt(index);
        if (index <= historyPosition_) --historyPosition_;
    }
    failed_.subtract(removed);
    model_.remove(ids);
    if (current) emit selectItem(model_.idAt(qMin(currentRow, model_.rowCount() - 1)));
    emit changed();
}
void PlaylistController::clear()
{
    resetAdvance();
    player_.stop(true);
    model_.clear();
    resetShuffle();
    emit changed();
}
