#include "playlistcontroller.h"
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>

PlaylistController::PlaylistController(Player &player, QObject *parent) : QObject(parent), model_(this), player_(player)
{
    connect(&player_, &Player::sessionChanged, this, &PlaylistController::session);
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
    if (replace) { automatic_ = false; failed_.clear(); history_.clear(); historyPosition_ = -1; bag_.clear(); roundSeen_.clear(); model_.clear(); }
    const bool wasEmpty = model_.rowCount() == 0;
    const auto ids = model_.insert(valid, row); prune();
    if (!ids.isEmpty() && !subtitles.isEmpty()) model_.setSubtitles(ids.first(), subtitles);
    if (invalid || valid.size() != ids.size()) emit message(QStringLiteral("加入 %1 项；跳过 %2 个无效项目、%3 个重复项目。")
        .arg(ids.size()).arg(invalid).arg(valid.size() - ids.size()));
    if (!ids.isEmpty() && (replace || wasEmpty)) start(ids.first(), false);
    emit changed(); return ids;
}
void PlaylistController::setOrder(Order order)
{
    if (order_ == order) return;
    order_ = order; history_.clear(); historyPosition_ = -1; bag_.clear(); roundSeen_.clear();
    if (model_.currentId()) { history_.append(model_.currentId()); historyPosition_ = 0; roundSeen_.insert(model_.currentId()); }
    prune(); emit changed();
}
void PlaylistController::prune()
{
    for (int i = history_.size() - 1; i >= 0; --i) if (!model_.item(history_[i])) { history_.removeAt(i); if (i <= historyPosition_) --historyPosition_; }
    for (auto it = bag_.begin(); it != bag_.end();) if (!model_.item(*it)) it = bag_.erase(it); else ++it;
    for (int row = 0; row < model_.rowCount(); ++row) {
        const auto id = model_.idAt(row); if (!roundSeen_.contains(id) && !bag_.contains(id)) bag_.append(id);
    }
}
void PlaylistController::refillBag()
{
    bag_.clear(); roundSeen_.clear();
    for (int row = 0; row < model_.rowCount(); ++row) bag_.append(model_.idAt(row));
}
quint64 PlaylistController::nextId(bool automatic)
{
    const int count = model_.rowCount(), row = model_.rowOf(model_.currentId());
    if (!count) return 0;
    if (order_ == Order::Random) {
        if (historyPosition_ + 1 < history_.size()) return history_[++historyPosition_];
        if (bag_.isEmpty()) refillBag();
        int selected = QRandomGenerator::global()->bounded(int(bag_.size()));
        if (bag_.size() > 1 && bag_[selected] == model_.currentId()) selected = (selected + 1) % bag_.size();
        const auto id = bag_.takeAt(selected); roundSeen_.insert(id); return id;
    }
    if (automatic && order_ == Order::SingleRepeat && row >= 0) return model_.currentId();
    if (row + 1 < count) return model_.idAt(row + 1);
    return order_ == Order::ListRepeat ? model_.idAt(0) : 0;
}
bool PlaylistController::canPrevious() const { return order_ == Order::Random ? historyPosition_ > 0 : model_.rowCount() > 0 && (model_.rowOf(model_.currentId()) > 0 || order_ == Order::ListRepeat); }
bool PlaylistController::canNext() const { return model_.rowCount() > 0 && (order_ == Order::Random || order_ == Order::ListRepeat || model_.rowOf(model_.currentId()) + 1 < model_.rowCount()); }
void PlaylistController::start(quint64 id, bool automatic, bool record)
{
    const auto *item = model_.item(id); if (!item) return;
    const QString path = item->path;
    automatic_ = automatic;
    if (!automatic) failed_.clear();
    if (order_ == Order::Random && record) {
        if (historyPosition_ < 0 || history_.value(historyPosition_) != id) {
            while (history_.size() > historyPosition_ + 1) history_.removeLast();
            history_.append(id); historyPosition_ = history_.size() - 1;
        }
        roundSeen_.insert(id); bag_.removeAll(id);
    }
    const auto before = model_.currentId();
    if (before != id) model_.setStatus(before, {});
    model_.setCurrent(id); emit selectItem(id);
    player_.openFile(path, id); emit changed();
}
void PlaylistController::play(quint64 id) { start(id, false); }
void PlaylistController::replay() { start(model_.currentId() ? model_.currentId() : model_.idAt(0), false); }
void PlaylistController::next() { const auto id = nextId(false); if (id) start(id, false, order_ != Order::Random || historyPosition_ == history_.size() - 1); }
void PlaylistController::previous()
{
    quint64 id = 0;
    if (order_ == Order::Random) { if (historyPosition_ > 0) id = history_[--historyPosition_]; }
    else { int row = model_.rowOf(model_.currentId()) - 1; if (row < 0 && order_ == Order::ListRepeat) row = model_.rowCount() - 1; id = model_.idAt(row); }
    if (id) start(id, false, false);
}
void PlaylistController::session(quint64 generation, quint64 id, Player::State state, const QString &error)
{
    if (!id || id != model_.currentId() || generation != player_.generation()) return;
    QString status;
    switch (state) {
    case Player::State::Loading: status = QStringLiteral("正在打开"); break;
    case Player::State::Loaded:
        status = QStringLiteral("正在播放"); failed_.clear();
        if (const auto *item = model_.item(id)) for (const auto &subtitle : item->subtitles) player_.addSubtitle(subtitle);
        automatic_ = false; break;
    case Player::State::Idle: status = QStringLiteral("已停止"); automatic_ = false; break;
    case Player::State::Ended: status = QStringLiteral("播放完毕"); automatic_ = true; failed_.clear(); break;
    case Player::State::Error: status = QStringLiteral("打开失败"); break;
    }
    model_.setStatus(id, status, error); emit changed();
    if (state != Player::State::Ended && !(state == Player::State::Error && automatic_)) return;
    if (state == Player::State::Error) { failed_.insert(id); emit message(QStringLiteral("跳过无法播放的文件：%1").arg(QFileInfo(model_.item(id)->path).fileName())); }
    QTimer::singleShot(0, this, [this, generation, id] {
        if (!automatic_ || player_.generation() != generation || model_.currentId() != id) return;
        const auto target = nextId(true);
        if (!target || failed_.contains(target)) { automatic_ = false; emit message(failed_.isEmpty() ? QStringLiteral("播放列表已结束") : QStringLiteral("没有可继续播放的项目，已停止自动续播。")); return; }
        start(target, true);
    });
}
void PlaylistController::remove(const QList<quint64> &ids)
{
    const int currentRow = model_.rowOf(model_.currentId()); const bool current = ids.contains(model_.currentId());
    if (current) { automatic_ = false; player_.stop(true); }
    model_.remove(ids); prune();
    if (current) emit selectItem(model_.idAt(qMin(currentRow, model_.rowCount() - 1)));
    emit changed();
}
void PlaylistController::clear()
{
    automatic_ = false; player_.stop(true); model_.clear(); history_.clear(); bag_.clear(); roundSeen_.clear(); failed_.clear(); historyPosition_ = -1; emit changed();
}
