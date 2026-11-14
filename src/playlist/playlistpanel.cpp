#include "playlistpanel.h"
#include "playlistmodel.h"
#include "playeractions.h"
#include <QAction>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

class PlaylistView final : public QListView
{
public:
    std::function<void(const QList<QUrl> &, int)> externalDrop;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); else QListView::dragEnterEvent(event); }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!event->mimeData()->hasUrls()) { QListView::dragMoveEvent(event); return; }
        externalRow_ = dropRow(event->position().toPoint()); setProperty("externalDropRow", externalRow_);
        viewport()->update(); event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override { externalRow_ = -1; viewport()->update(); QListView::dragLeaveEvent(event); }
    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasUrls()) { QListView::dropEvent(event); return; }
        externalRow_ = -1; viewport()->update();
        externalDrop(event->mimeData()->urls(), dropRow(event->position().toPoint())); event->acceptProposedAction();
    }
    void startDrag(Qt::DropActions supported) override { if (selectedIndexes().size() == 1) QListView::startDrag(supported); }
    void paintEvent(QPaintEvent *event) override
    {
        QListView::paintEvent(event); if (externalRow_ < 0) return;
        int y = externalRow_ < model()->rowCount() ? visualRect(model()->index(externalRow_, 0)).top()
            : model()->rowCount() ? visualRect(model()->index(model()->rowCount() - 1, 0)).bottom() + 1 : 2;
        y = qBound(2, y, viewport()->height() - 2);
        QPainter painter(viewport()); painter.setPen(QPen(palette().color(QPalette::Highlight), 2)); painter.drawLine(4, y, viewport()->width() - 4, y);
    }
private:
    int dropRow(QPoint position) const
    {
        const auto index = indexAt(position);
        return index.isValid() ? index.row() + (position.y() > visualRect(index).center().y() ? 1 : 0) : model()->rowCount();
    }
    int externalRow_ = -1;
};

class PlaylistDelegate final : public QStyledItemDelegate
{
public: using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem item(option); initStyleOption(&item, index);
        item.text = QStringLiteral("%1  %2").arg(index.row() + 1).arg(item.text);
        (item.widget ? item.widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
    }
};

PlaylistPanel::PlaylistPanel(PlaylistModel &model, PlayerActions &actions, QWidget *parent) : QWidget(parent), model_(model)
{
    setObjectName("playlistPanel"); setMinimumWidth(260); setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(10, 10, 10, 10); layout->setSpacing(8);
    auto *header = new QHBoxLayout; count_ = new QLabel; count_->setObjectName("playlistCount"); header->addWidget(count_, 1);
    auto makeButton = [&actions](PlayerActions::Id id, const QString &name, bool text = false) {
        auto *button = new QToolButton; button->setDefaultAction(actions.get(id)); button->setObjectName(name);
        button->setMinimumSize(32, 32); button->setFocusPolicy(Qt::StrongFocus);
        if (text) button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAccessibleName(actions.get(id)->text()); button->setAccessibleDescription(actions.get(id)->toolTip());
        QObject::connect(actions.get(id), &QAction::changed, button, [button, action = actions.get(id)] {
            button->setAccessibleName(action->text()); button->setAccessibleDescription(action->toolTip());
        }); return button;
    };
    header->addWidget(makeButton(PlayerActions::PlaylistMode, "playlistModeButton", true));
    header->addWidget(makeButton(PlayerActions::Playlist, "hidePlaylistButton")); layout->addLayout(header);
    auto *playbackOrder = new QHBoxLayout;
    auto *repeat = makeButton(PlayerActions::RepeatCycle, "playlistRepeatButton", true);
    auto *shuffle = makeButton(PlayerActions::ShuffleToggle, "playlistShuffleButton", true);
    for (auto *button : {repeat, shuffle}) {
        button->setProperty("playlistOrderControl", true);
        playbackOrder->addWidget(button, 1);
    }
    layout->addLayout(playbackOrder);
    empty_ = new QLabel(QStringLiteral("播放列表为空\n添加文件或拖入媒体")); empty_->setObjectName("playlistEmpty");
    empty_->setAlignment(Qt::AlignCenter); empty_->setWordWrap(true); layout->addWidget(empty_);
    auto *view = new PlaylistView; view_ = view; view_->setObjectName("playlistView"); view_->setAccessibleName(QStringLiteral("播放列表"));
    view_->setModel(&model); view_->setSelectionMode(QAbstractItemView::ExtendedSelection); view_->setUniformItemSizes(true);
    view_->setItemDelegate(new PlaylistDelegate(view_));
    view_->setDragDropMode(QAbstractItemView::InternalMove); view_->setDefaultDropAction(Qt::MoveAction); view_->setDragDropOverwriteMode(false);
    view_->setTextElideMode(Qt::ElideMiddle); view_->setDropIndicatorShown(true); layout->addWidget(view_, 1);
    view->externalDrop = [this](const auto &urls, int row) { emit filesDropped(urls, row); };
    connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex &index) { emit playItem(model_.idAt(index.row())); });
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { emit selectionChanged(); });
    auto *menu = new QMenu(view_);
    menu->setObjectName("playlistContextMenu");
    for (auto id : {PlayerActions::QueuePlay, PlayerActions::QueueUp, PlayerActions::QueueDown, PlayerActions::QueueRemove, PlayerActions::QueueClear,
                   PlayerActions::Add, PlayerActions::AddFolder, PlayerActions::PlaylistMode}) menu->addAction(actions.get(id));
    menu->addSeparator();
    actions.addPlaybackOrderActions(*menu);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested, this, [this, menu](QPoint point) {
        menu->popup(view_->viewport()->mapToGlobal(point.x() < 0 ? view_->visualRect(view_->currentIndex()).center() : point));
    });
    empty_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(empty_, &QWidget::customContextMenuRequested, this, [this, menu](QPoint point) {
        menu->popup(empty_->mapToGlobal(point));
    });
    scan_ = new QWidget; auto *scanLayout = new QHBoxLayout(scan_); scanLayout->setContentsMargins(0, 0, 0, 0);
    auto *progress = new QProgressBar; progress->setRange(0, 0); progress->setAccessibleName(QStringLiteral("正在扫描文件夹")); scanLayout->addWidget(progress, 1);
    scanLayout->addWidget(makeButton(PlayerActions::CancelScan, "cancelScanButton", true)); layout->addWidget(scan_); scan_->hide();
    auto *footer = new QHBoxLayout; footer->addWidget(makeButton(PlayerActions::Add, "addFilesButton", true)); footer->addWidget(makeButton(PlayerActions::AddFolder, "addFolderButton", true)); layout->addLayout(footer);
    const auto update = [this] { count_->setText(QStringLiteral("播放列表 · %1 项").arg(model_.rowCount())); empty_->setVisible(model_.rowCount() == 0); emit selectionChanged(); };
    connect(&model, &QAbstractItemModel::rowsInserted, this, update); connect(&model, &QAbstractItemModel::rowsRemoved, this, update); connect(&model, &QAbstractItemModel::modelReset, this, update); update();
}
QList<quint64> PlaylistPanel::selectedIds() const { QList<quint64> ids; for (const auto &index : view_->selectionModel()->selectedRows()) ids.append(model_.idAt(index.row())); return ids; }
quint64 PlaylistPanel::selectedId() const
{
    const auto ids = selectedIds(); const auto current = model_.idAt(view_->currentIndex().row());
    return ids.contains(current) ? current : ids.value(0);
}
void PlaylistPanel::select(quint64 id)
{
    const int row = model_.rowOf(id); if (row < 0) return;
    view_->selectionModel()->setCurrentIndex(model_.index(row), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows); view_->scrollTo(model_.index(row), QAbstractItemView::EnsureVisible);
}
void PlaylistPanel::setScanning(bool busy) { scan_->setVisible(busy); }
