#pragma once
#include "playlistmodel.h"
#include "player.h"
#include "repeatmode.h"
#include <QObject>
#include <QRandomGenerator>
#include <QSet>
#include <functional>
#include <optional>

class PlaylistController final : public QObject
{
    Q_OBJECT
public:
    using CompletionHandler = std::function<void(const Player::PlaybackIdentity &)>;
    explicit PlaylistController(Player &player, QObject *parent = nullptr, std::optional<quint32> shuffleSeed = {});
    PlaylistModel &model() { return model_; }
    RepeatMode repeatMode() const { return repeatMode_; }
    bool shuffleEnabled() const { return shuffleEnabled_; }
    void setPlaybackOrder(RepeatMode repeatMode, bool shuffleEnabled);
    void cycleRepeatMode();
    void setCompletionHandler(CompletionHandler handler);
    void advanceAfterCompletion(const Player::PlaybackIdentity &playback);
    QList<quint64> addFiles(const QStringList &paths, bool replace, int row = -1, const QStringList &subtitles = {});
    void play(quint64 id);
    void replay();
    void previous();
    void next();
    bool canPrevious() const;
    bool canNext() const;
    static constexpr int shuffleHistoryLimit = 1024;
    void remove(const QList<quint64> &ids);
    void clear();
signals:
    void changed();
    void playbackOrderChanged();
    void message(const QString &text);
    void selectItem(quint64 id);
private:
    void start(quint64 id, bool automatic, bool record = true);
    void advanceAutomatically();
    void scheduleAdvance(Player::PlaybackIdentity playback, Player::State state);
    bool rememberRandomItem(quint64 id);
    int previousRandomPosition() const;
    void session(quint64 generation, quint64 id, Player::State state, const QString &error);
    quint64 nextId(const QSet<quint64> *excluded = nullptr);
    void refillBag(const QSet<quint64> *excluded = nullptr);
    void resetShuffle();
    void resetAdvance();
    PlaylistModel model_;
    Player &player_;
    RepeatMode repeatMode_ = RepeatMode::None;
    bool shuffleEnabled_ = false;
    int attemptsRemaining_ = 0;
    QSet<quint64> failed_;
    bool skippedFailure_ = false;
    CompletionHandler completionHandler_;
    QRandomGenerator random_;
    QList<quint64> bag_, history_;
    int historyPosition_ = -1;
    bool pendingHistoryEntry_ = false;
};
