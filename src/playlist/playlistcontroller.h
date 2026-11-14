#pragma once
#include "playlistmodel.h"
#include "player.h"
#include "repeatmode.h"
#include <QObject>
#include <QSet>

class PlaylistController final : public QObject
{
    Q_OBJECT
public:
    explicit PlaylistController(Player &player, QObject *parent = nullptr);
    PlaylistModel &model() { return model_; }
    RepeatMode repeatMode() const { return repeatMode_; }
    bool shuffleEnabled() const { return shuffleEnabled_; }
    void setPlaybackOrder(RepeatMode repeatMode, bool shuffleEnabled);
    void cycleRepeatMode();
    QList<quint64> addFiles(const QStringList &paths, bool replace, int row = -1, const QStringList &subtitles = {});
    void play(quint64 id);
    void replay();
    void previous();
    void next();
    bool canPrevious() const;
    bool canNext() const;
    void remove(const QList<quint64> &ids);
    void clear();
signals:
    void changed();
    void message(const QString &text);
    void selectItem(quint64 id);
private:
    void start(quint64 id, bool automatic, bool record = true);
    void advance(bool automatic);
    void rememberRandomItem(quint64 id);
    void session(quint64 generation, quint64 id, Player::State state, const QString &error);
    quint64 nextId(bool automatic);
    void refillBag();
    void prune();
    PlaylistModel model_;
    Player &player_;
    RepeatMode repeatMode_ = RepeatMode::None;
    bool shuffleEnabled_ = false;
    bool automatic_ = false;
    QSet<quint64> failed_, roundSeen_;
    QList<quint64> bag_, history_;
    int historyPosition_ = -1;
};
