#include "playlistmodel.h"
#include <QFileInfo>
#include <QMimeData>
#include <QFont>
#include <QBrush>
#include <QApplication>
#include <QCollator>
#include <QDateTime>
#include <QPalette>
#include <QRandomGenerator>
#include <QSet>
#include <algorithm>
#include <numeric>
#include <tuple>

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

namespace {
QString details(const PlaylistModel::Item &item, bool reading)
{
    const auto &media = item.metadata;
    const auto unknown = QStringLiteral("未知");
    const auto pending = reading ? QStringLiteral("正在读取") : media.state == PlaylistMetadata::State::Unrequested
        ? QStringLiteral("未读取") : unknown;
    QStringList lines{
        QStringLiteral("标题：%1").arg(media.state == PlaylistMetadata::State::Read ? (media.title.isEmpty() ? item.name : media.title) : pending),
        QStringLiteral("时长：%1").arg(media.duration ? QStringLiteral("%1 秒").arg(QLocale().toString(*media.duration, 'f', 2)) : pending),
        QStringLiteral("分辨率：%1").arg(media.resolution.isEmpty() ? pending : QStringLiteral("%1 × %2").arg(media.resolution.width()).arg(media.resolution.height()))
    };
    if (item.file) {
        const auto date = [&unknown](std::optional<qint64> time) {
            return time ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(*time), QLocale::ShortFormat) : unknown;
        };
        lines << QStringLiteral("大小：%1").arg(item.file->bytes < 0 ? unknown : QLocale().formattedDataSize(item.file->bytes))
              << QStringLiteral("修改时间：%1").arg(date(item.file->modified))
              << QStringLiteral("创建时间：%1").arg(date(item.file->created));
    }
    return lines.join('\n');
}

template<class T>
int compare(const T &a, const T &b)
{
    return a < b ? -1 : b < a ? 1 : 0;
}
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= items_.size()) return {};
    const auto &item = items_[index.row()];
    switch (role) {
    case Qt::DisplayRole: return (item.id == current_ ? QStringLiteral("▶ ") : QString())
        + item.name + (item.status.isEmpty() ? QString() : "  · " + item.status);
    case Qt::ToolTipRole:
        return item.path + '\n' + details(item, item.id == readingMetadata_) + (item.error.isEmpty() ? QString() : '\n' + item.error);
    case Qt::AccessibleTextRole: return item.name + " " + item.status;
    case Qt::AccessibleDescriptionRole: return details(item, item.id == readingMetadata_);
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
        const QFileInfo file(path);
        seen.insert(key);
        additions.append({nextId_++, file.absoluteFilePath(), file.fileName(), file.absolutePath(), file.suffix().toLower()});
        ids.append(additions.last().id);
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

void PlaylistModel::setReadingMetadata(quint64 id)
{
    if (readingMetadata_ == id) return;
    const auto previous = readingMetadata_;
    readingMetadata_ = id;
    for (auto key : {previous, id}) {
        if (const int row = rowOf(key); row >= 0)
            emit dataChanged(index(row), index(row), {Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
    }
}

void PlaylistModel::setMetadata(quint64 id, const PlaylistFileInfo &file, const std::optional<PlaylistMetadata> &media)
{
    const int row = rowOf(id);
    if (row < 0) return;
    auto &item = items_[row];
    const bool versionChanged = !item.file || *item.file != file;
    if (versionChanged) {
        item.file = file;
        item.metadata = {};
    }
    if (media) item.metadata = *media;
    if (versionChanged || media) emit dataChanged(index(row), index(row), {Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
}

QList<int> PlaylistModel::rowOrder() const
{
    QList<int> order(rowCount());
    std::iota(order.begin(), order.end(), 0);
    return order;
}

void PlaylistModel::applyOrder(const QList<int> &order, QAbstractItemModel::LayoutChangeHint hint)
{
    bool same = true;
    for (int row = 0; row < order.size(); ++row) {
        if (order[row] != row) { same = false; break; }
    }
    if (same) return;

    emit layoutAboutToBeChanged({}, hint);
    const auto before = persistentIndexList();
    QList<int> newRows(rowCount());
    QVector<Item> reordered;
    reordered.reserve(rowCount());
    for (int row = 0; row < order.size(); ++row) {
        newRows[order[row]] = row;
        reordered.append(std::move(items_[order[row]]));
    }
    items_.swap(reordered);
    QModelIndexList after;
    after.reserve(before.size());
    // Each old row identifies one unchanged item ID in the permutation.
    for (const auto &old : before) after.append(index(newRows[old.row()]));
    changePersistentIndexList(before, after);
    emit layoutChanged({}, hint);
}

void PlaylistModel::sortItems(PlaylistSort sort)
{
    const QLocale locale;
    QCollator collator(locale.language() == QLocale::C ? QLocale(QLocale::English) : locale);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    const auto text = [&collator](const QString &a, const QString &b) { return collator.compare(a, b); };
    const auto group = [&text](const QString &a, const QString &b, const Item &left, const Item &right) {
        const int result = text(a, b);
        return result ? result : text(left.name, right.name);
    };
    const auto date = [](const Item &item, bool created) -> std::optional<qint64> {
        return item.file ? (created ? item.file->created : item.file->modified) : std::nullopt;
    };
    const auto resolution = [](const Item &item) {
        const auto size = item.metadata.resolution;
        return std::tuple{size.isEmpty() ? 0 : qint64(size.width()) * size.height(), size.width(), size.height()};
    };
    const auto known = [&](const Item &item) {
        switch (sort.field) {
        case SortField::Title: return item.metadata.state == PlaylistMetadata::State::Read;
        case SortField::Extension: return !item.extension.isEmpty();
        case SortField::FileSize: return item.file && item.file->bytes >= 0;
        case SortField::ModifiedTime: return date(item, false).has_value();
        case SortField::CreatedTime: return date(item, true).has_value();
        case SortField::Duration: return item.metadata.duration.has_value();
        case SortField::Resolution: return !item.metadata.resolution.isEmpty();
        default: return true;
        }
    };
    const auto orderFor = [&](const Item &a, const Item &b) {
        switch (sort.field) {
        case SortField::FileName: return text(a.name, b.name);
        case SortField::Title: return text(a.metadata.title.isEmpty() ? a.name : a.metadata.title,
                                          b.metadata.title.isEmpty() ? b.name : b.metadata.title);
        case SortField::Extension: return group(a.extension, b.extension, a, b);
        case SortField::Folder: return group(a.folder, b.folder, a, b);
        case SortField::Path: return text(a.path, b.path);
        case SortField::FileSize: return compare(a.file->bytes, b.file->bytes);
        case SortField::ModifiedTime: return compare(date(a, false), date(b, false));
        case SortField::CreatedTime: return compare(date(a, true), date(b, true));
        case SortField::Duration: return compare(a.metadata.duration, b.metadata.duration);
        case SortField::Resolution: return compare(resolution(a), resolution(b));
        case SortField::AddedOrder: return compare(a.id, b.id);
        }
        return 0;
    };
    auto order = rowOrder();
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        const auto &a = items_[left], &b = items_[right];
        const bool aKnown = known(a), bKnown = known(b);
        if (aKnown != bKnown) return aKnown;
        if (!aKnown) return false;
        const int comparison = orderFor(a, b);
        return sort.direction == SortDirection::Ascending ? comparison < 0 : comparison > 0;
    });
    applyOrder(order, VerticalSortHint);
}

void PlaylistModel::reverseItems()
{
    auto order = rowOrder();
    std::reverse(order.begin(), order.end());
    applyOrder(order);
}

void PlaylistModel::shuffleItems(QRandomGenerator &random)
{
    auto order = rowOrder();
    // Rejection sampling keeps each Fisher–Yates choice uniform even when its range isn't a power of two.
    for (int end = int(order.size()); end > 1; --end) {
        const auto bound = quint32(end);
        const quint32 cutoff = (quint32(0) - bound) % bound;
        quint32 value;
        do { value = random.generate(); } while (value < cutoff);
        std::swap(order[end - 1], order[value % bound]);
    }
    applyOrder(order);
}
