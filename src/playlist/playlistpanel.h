#pragma once
#include "playlistsort.h"
#include <QElapsedTimer>
#include <QWidget>
#include <QUrl>
#include <optional>
class QListView;
class QLabel;
class QLineEdit;
class QModelIndex;
class QProgressBar;
class PlayerActions;
class PlaylistFilter;
class PlaylistModel;
class PlaylistView;
class PlaylistPanel final : public QWidget
{
    Q_OBJECT
public:
    PlaylistPanel(PlaylistModel &model, PlayerActions &actions, QWidget *parent = nullptr);
    QListView *view() const;
    bool filtered() const;
    QList<quint64> selectedIds() const;
    quint64 selectedId() const;
    void select(quint64 id);
    void setScanning(bool busy);
    void setSortOrder(std::optional<PlaylistSort> sort);
    void setSortProgress(int done, int total, bool verifying);
signals:
    void playItem(quint64 id);
    void filesDropped(const QList<QUrl> &urls, int row);
    void selectionChanged();
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    QModelIndex visibleIndex(quint64 id) const;
    void rememberSelection();
    void filterList(const QString &query);
    void updateList();
    PlaylistModel &model_;
    PlaylistFilter *filter_;
    PlaylistView *view_;
    QLineEdit *search_;
    QList<quint64> rememberedIds_;
    quint64 rememberedCurrent_ = 0;
    bool applyingFilter_ = false;
    QLabel *empty_, *count_, *sortOrder_;
    QWidget *scan_, *sortProgressRow_;
    QProgressBar *sortProgress_;
    QElapsedTimer progressUpdate_;
};
