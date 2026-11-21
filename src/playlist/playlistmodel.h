#pragma once
#include "playlistmetadata.h"
#include "playlistsort.h"
#include <QAbstractListModel>
#include <QStringList>
#include <QVector>
#include <QUuid>

class QRandomGenerator;

namespace MediaFiles {
const QStringList &mediaExtensions();
bool media(const QString &path);
bool subtitle(const QString &path);
QString key(const QString &path);
}

class PlaylistModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Item {
        quint64 id; // Monotonically assigned on insertion, also the added-order key.
        QString path, name, folder, extension, status, error;
        QStringList subtitles;
        std::optional<PlaylistFileInfo> file;
        PlaylistMetadata metadata;
    };
    enum Role { IdRole = Qt::UserRole + 1, PathRole, StatusRole, TitleRole };
    explicit PlaylistModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(items_.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *mime, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    bool moveRows(const QModelIndex &, int source, int count, const QModelIndex &, int destination) override;
    const Item *item(quint64 id) const;
    int rowOf(quint64 id) const;
    quint64 idAt(int row) const;
    quint64 currentId() const { return current_; }
    QList<quint64> insert(const QStringList &paths, int row = -1);
    void remove(const QList<quint64> &ids);
    void clear();
    void setCurrent(quint64 id);
    void setStatus(quint64 id, const QString &status, const QString &error = {});
    void setSubtitles(quint64 id, const QStringList &paths);
    void moveSelected(const QList<quint64> &ids, int direction);
    void setReadingMetadata(quint64 id);
    void setMetadata(quint64 id, const PlaylistFileInfo &file, const std::optional<PlaylistMetadata> &media = {});
    void sortItems(PlaylistSort sort);
    void reverseItems();
    void shuffleItems(QRandomGenerator &random);
private:
    QList<int> rowOrder() const;
    void applyOrder(const QList<int> &order, QAbstractItemModel::LayoutChangeHint hint = NoLayoutChangeHint);
    QVector<Item> items_;
    quint64 nextId_ = 1, current_ = 0, readingMetadata_ = 0;
    QByteArray dragToken_ = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
};
