#pragma once
#include "metadataprobe.h"
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
    PlaylistSort lastSort() const { return lastSort_; }
    std::optional<PlaylistSort> appliedSort() const { return appliedSort_; }
    void setLastSort(PlaylistSort sort);
    void sortItems(PlaylistSort sort);
    void reverseItems();
    void shuffleItems();
    void cancelSort();
    void setCompletionHandler(CompletionHandler handler);
    void advanceAfterCompletion(const Player::PlaybackIdentity &playback);
    QList<quint64> addFiles(const QStringList &paths, bool replace, int row = -1);
    QList<quint64> importPlaylist(const QString &fileName, bool replace);
    bool exportPlaylist(const QString &fileName);
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
    void sortPreferenceChanged();
    void sortChanged();
    void sortProgress(int done, int total, bool verifying);
    void message(const QString &text);
    void selectItem(quint64 id);
private:
    enum class ImportKind { Media, Playlist };
    QList<quint64> addFiles(const QStringList &paths, bool replace, int row, ImportKind kind, QStringList skipped = {});
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
    void manualOrder();
    void readSortItem();
    void sortItemRead(quint64 request, const QString &path, const MetadataProbe::Result &result);
    bool verifySortVersions(bool cached = false);
    void sortVersionsChecked(quint64 request, const QVector<int> &changed, bool verified);
    void applySort(PlaylistSort sort);
    PlaylistModel model_;
    Player &player_;
    MetadataProbe probe_;
    struct SortJob {
        PlaylistSort sort;
        QList<quint64> ids;
        int done = 0;
        quint64 probeRequest = 0;
        bool checkingCache = false;
        QSet<quint64> needsRefresh;
    };
    std::optional<SortJob> sortJob_;
    PlaylistSort lastSort_;
    std::optional<PlaylistSort> appliedSort_;
    RepeatMode repeatMode_ = RepeatMode::None;
    bool shuffleEnabled_ = false;
    int attemptsRemaining_ = 0;
    QSet<quint64> failed_;
    bool skippedFailure_ = false;
    CompletionHandler completionHandler_;
    QRandomGenerator random_;
    QRandomGenerator reorderRandom_;
    QList<quint64> bag_, history_;
    int historyPosition_ = -1;
    bool pendingHistoryEntry_ = false;
};
