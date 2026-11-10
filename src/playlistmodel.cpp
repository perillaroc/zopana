#include "playlistmodel.h"
#include <QFileInfo>
#include <QMimeData>
#include <QFont>
#include <QBrush>
#include <QApplication>
#include <QPalette>
#include <QSet>
#include <algorithm>

namespace MediaFiles {
bool media(const QString &path)
{
    static const QStringList extensions{"mkv","mp4","mov","avi","webm","m4v","ts","m2ts","mpeg","mpg","wmv","flv","m4a","mp3","flac","wav","ogg","opus","aac","wma"};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
bool subtitle(const QString &path) { return QStringList{"srt","ass","ssa","vtt"}.contains(QFileInfo(path).suffix().toLower()); }
QString key(const QString &path)
{
    const QFileInfo file(path); const QString canonical = file.canonicalFilePath();
    return (canonical.isEmpty() ? file.absoluteFilePath() : canonical).toCaseFolded();
}
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    const auto &item = items_[index.row()];
    switch (role) {
    case Qt::DisplayRole: return (item.id == current_ ? QStringLiteral("▶ ") : QString())
        + QFileInfo(item.path).fileName() + (item.status.isEmpty() ? QString() : "  · " + item.status);
    case Qt::ToolTipRole: return item.path + (item.error.isEmpty() ? QString() : '\n' + item.error);
    case Qt::AccessibleTextRole: return QFileInfo(item.path).fileName() + " " + item.status;
    case Qt::FontRole: { QFont font = qApp->font(); font.setBold(item.id == current_); return font; }
    case Qt::ForegroundRole: if (item.id == current_) return QBrush(qApp->palette().color(QPalette::Highlight)); return {};
    case IdRole: return QVariant::fromValue(item.id);
    case PathRole: return item.path;
    case StatusRole: return item.status;
    default: return {};
    }
}
Qt::ItemFlags PlaylistModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled : Qt::ItemIsDropEnabled;
}
QStringList PlaylistModel::mimeTypes() const { return {"application/x-zopana-item"}; }
QMimeData *PlaylistModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mime = new QMimeData;
    if (indexes.size() == 1) mime->setData("application/x-zopana-item", dragToken_ + ':' + QByteArray::number(idAt(indexes.first().row())));
    return mime;
}
bool PlaylistModel::dropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int, const QModelIndex &parent)
{
    if (action == Qt::IgnoreAction) return true;
    const auto data = mime->data("application/x-zopana-item");
    if (action != Qt::MoveAction || !data.startsWith(dragToken_ + ':')) return false;
    const int source = rowOf(data.sliced(dragToken_.size() + 1).toULongLong());
    if (row < 0) row = parent.isValid() ? parent.row() : rowCount();
    return moveRows({}, source, 1, {}, row);
}
bool PlaylistModel::moveRows(const QModelIndex &sourceParent, int source, int count, const QModelIndex &destinationParent, int destination)
{
    if (sourceParent.isValid() || destinationParent.isValid() || count != 1 || source < 0 || source >= rowCount()
        || destination < 0 || destination > rowCount() || destination == source || destination == source + 1) return false;
    if (!beginMoveRows({}, source, source, {}, destination)) return false;
    items_.move(source, destination > source ? destination - 1 : destination); endMoveRows();
    emit dataChanged(index(0), index(rowCount() - 1), {Qt::DisplayRole}); return true;
}
const PlaylistModel::Item *PlaylistModel::item(quint64 id) const { const int row = rowOf(id); return row < 0 ? nullptr : &items_[row]; }
int PlaylistModel::rowOf(quint64 id) const { for (int row = 0; row < items_.size(); ++row) if (items_[row].id == id) return row; return -1; }
quint64 PlaylistModel::idAt(int row) const { return row >= 0 && row < items_.size() ? items_[row].id : 0; }
QList<quint64> PlaylistModel::insert(const QStringList &paths, int row)
{
    QSet<QString> seen; for (const auto &item : items_) seen.insert(MediaFiles::key(item.path));
    QVector<Item> additions; QList<quint64> ids;
    for (const auto &path : paths) {
        const auto key = MediaFiles::key(path); if (seen.contains(key)) continue;
        seen.insert(key); additions.append({nextId_++, QFileInfo(path).absoluteFilePath(), {}, {}, {}}); ids.append(additions.last().id);
    }
    if (additions.isEmpty()) return ids;
    row = row < 0 ? rowCount() : qBound(0, row, rowCount());
    beginInsertRows({}, row, row + int(additions.size()) - 1);
    for (int i = 0; i < additions.size(); ++i) items_.insert(row + i, additions[i]); endInsertRows();
    emit dataChanged(index(0), index(rowCount() - 1), {Qt::DisplayRole}); return ids;
}
void PlaylistModel::remove(const QList<quint64> &ids)
{
    for (int row = rowCount() - 1; row >= 0; --row) if (ids.contains(items_[row].id)) {
        beginRemoveRows({}, row, row); items_.removeAt(row); endRemoveRows();
    }
    if (!item(current_)) current_ = 0;
    if (rowCount()) emit dataChanged(index(0), index(rowCount() - 1));
}
void PlaylistModel::clear() { beginResetModel(); items_.clear(); current_ = 0; endResetModel(); }
void PlaylistModel::setCurrent(quint64 id)
{
    const auto before = current_; current_ = item(id) ? id : 0;
    for (auto key : {before, current_}) if (const int row = rowOf(key); row >= 0) emit dataChanged(index(row), index(row));
}
void PlaylistModel::setStatus(quint64 id, const QString &status, const QString &error)
{
    const int row = rowOf(id); if (row < 0) return;
    items_[row].status = status; items_[row].error = error; emit dataChanged(index(row), index(row));
}
void PlaylistModel::setSubtitles(quint64 id, const QStringList &paths) { const int row = rowOf(id); if (row >= 0) items_[row].subtitles = paths; }
void PlaylistModel::moveSelected(const QList<quint64> &ids, int direction)
{
    if (direction < 0) { for (int row = 1; row < rowCount(); ++row) if (ids.contains(idAt(row)) && !ids.contains(idAt(row - 1))) moveRows({}, row, 1, {}, row - 1); }
    else { for (int row = rowCount() - 2; row >= 0; --row) if (ids.contains(idAt(row)) && !ids.contains(idAt(row + 1))) moveRows({}, row, 1, {}, row + 2); }
}
