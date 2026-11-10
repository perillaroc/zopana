#pragma once
#include <QWidget>
#include <QUrl>
class QListView;
class QLabel;
class QProgressBar;
class PlayerActions;
class PlaylistModel;
class PlaylistPanel final : public QWidget
{
    Q_OBJECT
public:
    PlaylistPanel(PlaylistModel &model, PlayerActions &actions, QWidget *parent = nullptr);
    QListView *view() const { return view_; }
    QList<quint64> selectedIds() const;
    quint64 selectedId() const;
    void select(quint64 id);
    void setScanning(bool busy);
signals:
    void playItem(quint64 id);
    void filesDropped(const QList<QUrl> &urls, int row);
    void selectionChanged();
private:
    PlaylistModel &model_;
    QListView *view_;
    QLabel *empty_, *count_;
    QWidget *scan_;
};
