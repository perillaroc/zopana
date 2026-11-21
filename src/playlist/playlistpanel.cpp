#include "playlistpanel.h"
#include "playlistmodel.h"
#include "playeractions.h"
#include <QAction>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QScopedValueRollback>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QApplication>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

class PlaylistFilter final : public QSortFilterProxyModel
{
public:
    explicit PlaylistFilter(PlaylistModel &model, QObject *parent) : QSortFilterProxyModel(parent)
    {
        // Only cached title changes call for re-filtering existing rows.
        setDynamicSortFilter(false);
        setSourceModel(&model);
    }
    QString query() const { return query_; }
    void setQuery(const QString &query)
    {
        beginFilterChange();
        query_ = query;
        pathQuery_ = QDir::fromNativeSeparators(query);
        endFilterChange(Direction::Rows);
    }
    bool moveRows(const QModelIndex &source, int row, int count, const QModelIndex &destination, int before) override
    {
        // Without filtering, proxy rows follow the source order one for one.
        return query_.isEmpty() && sourceModel()->moveRows(mapToSource(source), row, count, mapToSource(destination), before);
    }
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override
    {
        return query_.isEmpty() && QSortFilterProxyModel::dropMimeData(data, action, row, column, parent);
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (query_.isEmpty()) return true;
        const auto item = sourceModel()->index(row, 0, parent);
        return item.data(PlaylistModel::PathRole).toString().contains(pathQuery_, Qt::CaseInsensitive)
            || item.data(PlaylistModel::TitleRole).toString().contains(query_, Qt::CaseInsensitive);
    }
private:
    QString query_, pathQuery_;
};

namespace {
quint64 itemId(const QModelIndex &index) { return index.data(PlaylistModel::IdRole).toULongLong(); }
}

class PlaylistView final : public QListView
{
public:
    std::function<void(const QList<QUrl> &, int)> externalDrop;
    void setFiltering(bool filtering)
    {
        setDragDropMode(filtering ? DropOnly : InternalMove);
        setExternalRow(-1);
    }
    void rememberScrollPosition() { topItem_ = indexAt(QPoint(viewport()->width() / 2, 1)); }
    void restoreScrollPosition()
    {
        if (topItem_.isValid()) scrollTo(topItem_, PositionAtTop);
        topItem_ = {};
    }
protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
        else if (dragEnabled()) QListView::dragEnterEvent(event);
        else event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!event->mimeData()->hasUrls()) {
            if (dragEnabled()) QListView::dragMoveEvent(event);
            else event->ignore();
            return;
        }
        setExternalRow(dropRow(event->position().toPoint()));
        event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override { setExternalRow(-1); QListView::dragLeaveEvent(event); }
    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasUrls()) {
            if (dragEnabled()) QListView::dropEvent(event);
            else event->ignore();
            return;
        }
        setExternalRow(-1);
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
    void setExternalRow(int row)
    {
        externalRow_ = row;
        setProperty("externalDropRow", row);
        viewport()->update();
    }
    int dropRow(QPoint position) const
    {
        if (!dragEnabled()) return -1;
        const auto index = indexAt(position);
        return index.isValid() ? index.row() + (position.y() > visualRect(index).center().y() ? 1 : 0) : model()->rowCount();
    }
    int externalRow_ = -1;
    QPersistentModelIndex topItem_;
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

PlaylistPanel::PlaylistPanel(PlaylistModel &model, PlayerActions &actions, QWidget *parent)
    : QWidget(parent), model_(model), filter_(new PlaylistFilter(model, this))
{
    setObjectName("playlistPanel"); setMinimumWidth(260); setAutoFillBackground(true);
    setAcceptDrops(true);
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
    search_ = new QLineEdit;
    search_->setObjectName("playlistSearch");
    search_->setAccessibleName(QStringLiteral("搜索播放列表"));
    search_->setPlaceholderText(QStringLiteral("搜索文件名、路径或媒体标题"));
    search_->setClearButtonEnabled(true);
    search_->setAcceptDrops(false);
    layout->addWidget(search_);
    auto *playbackOrder = new QHBoxLayout;
    auto *repeat = makeButton(PlayerActions::RepeatCycle, "playlistRepeatButton", true);
    auto *shuffle = makeButton(PlayerActions::ShuffleToggle, "playlistShuffleButton", true);
    for (auto *button : {repeat, shuffle}) {
        button->setProperty("playlistOrderControl", true);
        playbackOrder->addWidget(button, 1);
    }
    layout->addLayout(playbackOrder);
    auto *sorting = new QHBoxLayout;
    auto *sortButton = new QToolButton;
    sortButton->setObjectName("playlistSortButton");
    sortButton->setText(QStringLiteral("排序"));
    sortButton->setAccessibleName(QStringLiteral("列表排序"));
    sortButton->setMinimumHeight(32);
    sortButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    sortButton->setFocusPolicy(Qt::StrongFocus);
    sortButton->setPopupMode(QToolButton::InstantPopup);
    sortButton->setMenu(actions.playlistSortMenu(sortButton));
    sorting->addWidget(sortButton);
    sortOrder_ = new QLabel;
    sortOrder_->setObjectName("playlistSortOrder");
    sorting->addWidget(sortOrder_, 1);
    layout->addLayout(sorting);
    setSortOrder({});
    empty_ = new QLabel(QStringLiteral("播放列表为空\n添加文件或拖入媒体")); empty_->setObjectName("playlistEmpty");
    empty_->setAlignment(Qt::AlignCenter); empty_->setWordWrap(true); layout->addWidget(empty_);
    view_ = new PlaylistView;
    view_->setObjectName("playlistView"); view_->setAccessibleName(QStringLiteral("播放列表"));
    view_->setModel(filter_); view_->setSelectionMode(QAbstractItemView::ExtendedSelection); view_->setUniformItemSizes(true);
    view_->setItemDelegate(new PlaylistDelegate(view_));
    view_->setDragDropMode(QAbstractItemView::InternalMove); view_->setDefaultDropAction(Qt::MoveAction); view_->setDragDropOverwriteMode(false);
    view_->setTextElideMode(Qt::ElideMiddle); view_->setDropIndicatorShown(true); layout->addWidget(view_, 1);
    connect(filter_, &QAbstractItemModel::layoutAboutToBeChanged, view_, &PlaylistView::rememberScrollPosition);
    connect(filter_, &QAbstractItemModel::layoutChanged, view_, &PlaylistView::restoreScrollPosition);
    view_->externalDrop = [this](const auto &urls, int row) { emit filesDropped(urls, row); };
    connect(view_, &QListView::doubleClicked, this, [this](const QModelIndex &index) { emit playItem(itemId(index)); });
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PlaylistPanel::rememberSelection);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, &PlaylistPanel::rememberSelection);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString &text) {
        const auto query = text.trimmed();
        if (query != filter_->query()) filterList(query);
    });
    connect(&model, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (filtered() && roles.contains(PlaylistModel::TitleRole)) filterList(filter_->query());
    });
    auto *menu = new QMenu(view_);
    menu->setObjectName("playlistContextMenu");
    for (auto id : {PlayerActions::QueuePlay, PlayerActions::QueueUp, PlayerActions::QueueDown, PlayerActions::QueueRemove, PlayerActions::QueueClear,
                   PlayerActions::Add, PlayerActions::AddFolder, PlayerActions::PlaylistMode}) menu->addAction(actions.get(id));
    menu->addSeparator();
    for (auto id : {PlayerActions::OpenPlaylistFile, PlayerActions::AppendPlaylistFile, PlayerActions::ExportPlaylistFile}) menu->addAction(actions.get(id));
    menu->addSeparator();
    actions.addPlaybackOrderActions(*menu);
    menu->addMenu(actions.playlistSortMenu(menu));
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
    sortProgressRow_ = new QWidget;
    auto *sortProgressLayout = new QHBoxLayout(sortProgressRow_);
    sortProgressLayout->setContentsMargins(0, 0, 0, 0);
    sortProgress_ = new QProgressBar;
    sortProgress_->setObjectName("playlistSortProgress");
    sortProgress_->setAccessibleName(QStringLiteral("排序信息读取进度"));
    sortProgress_->setFormat(QStringLiteral("读取 %v / %m"));
    sortProgressLayout->addWidget(sortProgress_, 1);
    sortProgressLayout->addWidget(makeButton(PlayerActions::CancelSort, "playlistCancelSortButton", true));
    layout->addWidget(sortProgressRow_);
    sortProgressRow_->hide();
    auto *footer = new QHBoxLayout; footer->addWidget(makeButton(PlayerActions::Add, "addFilesButton", true)); footer->addWidget(makeButton(PlayerActions::AddFolder, "addFolderButton", true)); layout->addLayout(footer);
    connect(&model, &QAbstractItemModel::rowsInserted, this, &PlaylistPanel::updateList);
    connect(&model, &QAbstractItemModel::rowsRemoved, this, &PlaylistPanel::updateList);
    connect(&model, &QAbstractItemModel::modelReset, this, [this] {
        rememberedIds_.clear();
        rememberedCurrent_ = 0;
        updateList();
    });
    updateList();
}

QListView *PlaylistPanel::view() const { return view_; }
bool PlaylistPanel::filtered() const { return !filter_->query().isEmpty(); }

QList<quint64> PlaylistPanel::selectedIds() const
{
    QList<quint64> ids;
    for (const auto &index : view_->selectionModel()->selectedRows()) ids.append(itemId(index));
    return ids;
}
quint64 PlaylistPanel::selectedId() const
{
    const auto ids = selectedIds(); const auto current = itemId(view_->currentIndex());
    return ids.contains(current) ? current : ids.value(0);
}

QModelIndex PlaylistPanel::visibleIndex(quint64 id) const
{
    return filter_->mapFromSource(model_.index(model_.rowOf(id)));
}

void PlaylistPanel::select(quint64 id)
{
    const auto index = visibleIndex(id);
    if (!index.isValid()) return;
    view_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view_->scrollTo(index, QAbstractItemView::EnsureVisible);
}

void PlaylistPanel::rememberSelection()
{
    if (applyingFilter_) return;
    rememberedIds_ = selectedIds();
    const auto current = itemId(view_->currentIndex());
    rememberedCurrent_ = rememberedIds_.contains(current) ? current : rememberedIds_.value(0);
    emit selectionChanged();
}

void PlaylistPanel::filterList(const QString &query)
{
    {
        const QScopedValueRollback guard(applyingFilter_, true);
        filter_->setQuery(query);
        view_->setFiltering(filtered());
        const QSet<quint64> remembered(rememberedIds_.cbegin(), rememberedIds_.cend());
        QItemSelection visible;
        for (int row = 0; !remembered.isEmpty() && row < filter_->rowCount(); ++row) {
            const auto index = filter_->index(row, 0);
            if (remembered.contains(itemId(index))) visible.select(index, index);
        }
        auto *selection = view_->selectionModel();
        selection->select(visible, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        auto current = visibleIndex(rememberedCurrent_);
        if (!current.isValid() && !visible.isEmpty()) current = visible.first().topLeft();
        selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
        if (current.isValid()) view_->scrollTo(current, QAbstractItemView::EnsureVisible);
    }
    updateList();
}

void PlaylistPanel::updateList()
{
    const int visible = filter_->rowCount(), total = model_.rowCount();
    count_->setText(filtered() ? QStringLiteral("播放列表 · %1 / %2 项").arg(visible).arg(total)
        : QStringLiteral("播放列表 · %1 项").arg(total));
    empty_->setText(total ? QStringLiteral("没有匹配的项目") : QStringLiteral("播放列表为空\n添加文件或拖入媒体"));
    empty_->setVisible(!visible);
    emit selectionChanged();
}

void PlaylistPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void PlaylistPanel::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    emit filesDropped(event->mimeData()->urls(), -1);
    event->acceptProposedAction();
}
void PlaylistPanel::setScanning(bool busy) { scan_->setVisible(busy); }

void PlaylistPanel::setSortOrder(std::optional<PlaylistSort> sort)
{
    sortOrder_->setText(PlayerActions::sortLabel(sort));
}

void PlaylistPanel::setSortProgress(int done, int total, bool verifying)
{
    if (!total) {
        sortProgressRow_->hide();
        return;
    }
    const int maximum = verifying ? 0 : total;
    if (sortProgressRow_->isHidden() || sortProgress_->maximum() != maximum || !done) {
        sortProgress_->setRange(0, maximum);
        sortProgress_->setAccessibleName(verifying ? QStringLiteral("正在核对文件版本") : QStringLiteral("排序信息读取进度"));
        sortProgress_->setValue(0);
        sortProgressRow_->show();
    }
    if (!verifying && (done == total || !progressUpdate_.isValid() || progressUpdate_.elapsed() >= 80)) {
        sortProgress_->setValue(done);
        progressUpdate_.start();
    }
}
