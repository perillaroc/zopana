#include "settingsdialog.h"
#include "playeractions.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Preferences preferences,PlayerActions &actions,std::function<bool(const Preferences &,bool resetSubtitleScale)> apply,std::function<void()> clear,QWidget *parent)
    : QDialog(parent),draft_(std::move(preferences)),actions_(actions),apply_(std::move(apply))
{
    setObjectName("settingsDialog"); setWindowTitle(QStringLiteral("设置")); resize(760,620);
    auto *layout=new QVBoxLayout(this); auto *tabs=new QTabWidget; tabs->setObjectName("settingsTabs"); layout->addWidget(tabs);
    auto *general=new QWidget; auto *form=new QFormLayout(general); tabs->addTab(general,QStringLiteral("播放与记录"));
    capture_=new QLineEdit(draft_.captureDirectory); capture_->setObjectName("captureDirectoryEdit"); capture_->setAccessibleName(QStringLiteral("截图目录"));
    auto *folder=new QWidget; auto *row=new QHBoxLayout(folder); row->setContentsMargins(0,0,0,0); row->addWidget(capture_); auto *browse=new QPushButton(QStringLiteral("浏览…")); row->addWidget(browse);
    connect(browse,&QPushButton::clicked,this,[this] { const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择截图目录"),capture_->text()); if(!path.isEmpty()) capture_->setText(path); }); form->addRow(QStringLiteral("截图目录"),folder);
    audio_=new QLineEdit(draft_.audioLanguages); audio_->setObjectName("audioLanguagesEdit"); sub_=new QLineEdit(draft_.subtitleLanguages); sub_->setObjectName("subtitleLanguagesEdit");
    for(auto *edit:{audio_,sub_}) edit->setPlaceholderText(QStringLiteral("例如 eng,zho；留空使用媒体默认"));
    form->addRow(QStringLiteral("优先音轨语言"),audio_); form->addRow(QStringLiteral("优先字幕语言"),sub_);
    auto *languageNote=new QLabel(QStringLiteral("语言偏好按顺序用于下次打开媒体；当前轨道仍可从菜单手动选择。")); languageNote->setWordWrap(true); form->addRow(languageNote);
    history_=new QCheckBox(QStringLiteral("记录最近文件和播放进度")); history_->setObjectName("historyEnabledCheck"); history_->setChecked(draft_.recordHistory); form->addRow(history_);
    resume_=new QComboBox; resume_->setObjectName("resumePolicyCombo"); resume_->addItems({QStringLiteral("询问是否继续"),QStringLiteral("总是继续有效断点"),QStringLiteral("始终从头播放")}); resume_->setCurrentIndex(draft_.resumePolicy); form->addRow(QStringLiteral("断点策略"),resume_);
    auto *historyNote=new QLabel(QStringLiteral("仅在播放超过 30 秒且距离结尾超过 30 秒时恢复。关闭记录保留旧记录；清除立即生效，并暂停当前文件的记录，直到再次显式打开。")); historyNote->setWordWrap(true); form->addRow(historyNote);
    auto *clearButton=new QPushButton(QStringLiteral("立即清除最近文件和进度")); clearButton->setObjectName("clearHistoryButton"); form->addRow(clearButton);
    connect(clearButton,&QPushButton::clicked,this,[this,clear] { clear(); message_->setText(QStringLiteral("最近文件和进度已清除；此操作已立即生效。")); });
    auto *shortcuts=new QWidget; auto *keysLayout=new QVBoxLayout(shortcuts); tabs->addTab(shortcuts,QStringLiteral("快捷键"));
    filter_=new QLineEdit; filter_->setObjectName("shortcutFilter"); filter_->setPlaceholderText(QStringLiteral("过滤分类、命令或快捷键")); keysLayout->addWidget(filter_);
    table_=new QTableWidget(0,3); table_->setObjectName("shortcutTable"); table_->setHorizontalHeaderLabels({QStringLiteral("分类"),QStringLiteral("操作"),QStringLiteral("快捷键")}); table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->horizontalHeader()->setStretchLastSection(true); table_->verticalHeader()->hide(); keysLayout->addWidget(table_);
    auto *record=new QHBoxLayout; first_=new QKeySequenceEdit; first_->setObjectName("shortcutFirst"); second_=new QKeySequenceEdit; second_->setObjectName("shortcutSecond");
    for(auto *edit:{first_,second_}) { edit->setMaximumSequenceLength(1); edit->setAccessibleName(edit==first_?QStringLiteral("第一组合键"):QStringLiteral("第二组合键")); record->addWidget(edit); }
    auto *set=new QPushButton(QStringLiteral("设置绑定")); set->setObjectName("setBindingButton"); record->addWidget(set); keysLayout->addLayout(record); connect(set,&QPushButton::clicked,this,&SettingsDialog::bind);
    auto *resetRow=new QHBoxLayout; auto *empty=new QPushButton(QStringLiteral("清空此项")),*reset=new QPushButton(QStringLiteral("恢复此项")),*all=new QPushButton(QStringLiteral("恢复全部快捷键"));
    resetRow->addWidget(empty); resetRow->addWidget(reset); resetRow->addWidget(all); keysLayout->addLayout(resetRow);
    const auto editable=[this]() -> QString { const auto *item=table_->item(table_->currentRow(),1); return item?item->data(Qt::UserRole).toString():QString(); };
    connect(empty,&QPushButton::clicked,this,[this,editable] { const auto id=editable(); if(!id.isEmpty() && actions_.bindingError(id,{}).isEmpty()) { draft_.shortcuts[id]=QStringList(); populate(); } });
    connect(reset,&QPushButton::clicked,this,[this,editable] { draft_.shortcuts.remove(editable()); populate(); });
    connect(all,&QPushButton::clicked,this,[this] { for(const auto &id:actions_.defaultBindings().keys()) draft_.shortcuts.remove(id); populate(); });
    connect(table_,&QTableWidget::currentCellChanged,this,[this] { select(); }); connect(filter_,&QLineEdit::textChanged,this,[this] { populate(); });
    auto *note=new QLabel(QStringLiteral("点击录制框后按组合键，再设置绑定。F11/Enter 全屏、Esc 和 Tab 保持固定；取消不应用草稿。")); note->setWordWrap(true); keysLayout->addWidget(note);
    message_=new QLabel; message_->setObjectName("settingsMessage"); message_->setWordWrap(true); layout->addWidget(message_);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Apply|QDialogButtonBox::RestoreDefaults); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); buttons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("应用")); buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("恢复默认"));
    connect(buttons,&QDialogButtonBox::accepted,this,[this] { this->apply(true); }); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this] { this->apply(false); });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,this,[this] { const auto p=SettingsStore::defaults(); capture_->setText(p.captureDirectory); audio_->clear(); sub_->clear(); history_->setChecked(p.recordHistory); resume_->setCurrentIndex(p.resumePolicy); resetSubtitleScale_=true; for(const auto &id:actions_.defaultBindings().keys()) draft_.shortcuts.remove(id); populate(); message_->setText(QStringLiteral("已在草稿中恢复默认；应用或确定后生效。")); });
    populate();
}
void SettingsDialog::populate()
{
    const auto *old=table_->item(table_->currentRow(),1); const auto selected=old?old->data(Qt::UserRole).toString():QString();
    const QSignalBlocker blocker(table_); table_->setRowCount(0); int selectRow=0;
    for(int i=0;i<PlayerActions::Count;++i) {
        auto *action=actions_.get(PlayerActions::Id(i)); const auto id=action->objectName(); const auto keys=draft_.shortcuts.value(id,actions_.defaultBindings().value(id)).toStringList();
        const QMap<QString,QString> groups{{"file",QStringLiteral("文件")},{"play",QStringLiteral("播放")},{"seek",QStringLiteral("定位")},{"rate",QStringLiteral("倍速")},{"frame",QStringLiteral("帧步进")},{"loop",QStringLiteral("循环")},{"audio",QStringLiteral("音频")},{"sub",QStringLiteral("字幕")},{"view",QStringLiteral("画面")},{"window",QStringLiteral("窗口")},{"queue",QStringLiteral("队列")},{"capture",QStringLiteral("截图")},{"app",QStringLiteral("应用")}};
        const auto group=groups.value(id.section('.',0,0),QStringLiteral("其他"));
        QStringList native; for(const auto &key:keys) native.append(QKeySequence::fromString(key,QKeySequence::PortableText).toString(QKeySequence::NativeText));
        if(!(group+' '+id+' '+action->text()+' '+native.join(' ')).contains(filter_->text(),Qt::CaseInsensitive)) continue;
        const int row=table_->rowCount(); table_->insertRow(row); table_->setItem(row,0,new QTableWidgetItem(group)); auto *name=new QTableWidgetItem(action->text()); name->setData(Qt::UserRole,id); table_->setItem(row,1,name); table_->setItem(row,2,new QTableWidgetItem(native.join(" / "))); if(id==selected) selectRow=row;
    }
    table_->resizeColumnToContents(0); table_->resizeColumnToContents(1); if(table_->rowCount()) table_->selectRow(selectRow); select();
}
void SettingsDialog::select()
{
    const auto *item=table_->item(table_->currentRow(),1); if(!item) return; const auto id=item->data(Qt::UserRole).toString(); const auto keys=draft_.shortcuts.value(id,actions_.defaultBindings().value(id)).toStringList();
    first_->setKeySequence(QKeySequence::fromString(keys.value(0),QKeySequence::PortableText)); second_->setKeySequence(QKeySequence::fromString(keys.value(1),QKeySequence::PortableText));
    const bool editable=id!="window.fullscreen" && id!="window.escape"; first_->setEnabled(editable); second_->setEnabled(editable);
}
void SettingsDialog::bind()
{
    const auto *item=table_->item(table_->currentRow(),1); if(!item) return; const auto id=item->data(Qt::UserRole).toString(); QStringList keys;
    for(auto *edit:{first_,second_}) if(!edit->keySequence().isEmpty()) { const auto key=edit->keySequence().toString(QKeySequence::PortableText); if(!keys.contains(key)) keys.append(key); }
    const auto error=actions_.bindingError(id,keys); if(!error.isEmpty()) { message_->setText(error); return; }
    const auto conflicts=actions_.conflicts(id,keys,draft_.shortcuts);
    if(conflicts.contains("window.fullscreen") || conflicts.contains("window.escape")) { message_->setText(QStringLiteral("与固定的全屏或退出按键冲突，请选择其他组合。")); return; }
    if(!conflicts.isEmpty()) {
        QStringList names; for(const auto &other:conflicts) for(int i=0;i<PlayerActions::Count;++i) { auto *action=actions_.get(PlayerActions::Id(i)); if(action->objectName()==other) names.append(action->text()); }
        QMessageBox box(QMessageBox::Question,QStringLiteral("快捷键冲突"),QStringLiteral("与以下操作冲突：%1").arg(names.join(QStringLiteral("、"))),QMessageBox::NoButton,this);
        auto *replace=box.addButton(QStringLiteral("替换原绑定"),QMessageBox::AcceptRole); box.addButton(QStringLiteral("返回修改"),QMessageBox::RejectRole); box.exec(); if(box.clickedButton()!=replace) return;
        for(const auto &other:conflicts) { auto previous=draft_.shortcuts.value(other,actions_.defaultBindings().value(other)).toStringList(); previous.removeIf([&](const QString &old) { for(const auto &key:keys) if(PlayerActions::normalizedKey(key)==PlayerActions::normalizedKey(old)) return true; return false; }); draft_.shortcuts[other]=previous; }
    }
    draft_.shortcuts[id]=keys; message_->setText(QStringLiteral("绑定已加入草稿，应用后生效。")); populate();
}
void SettingsDialog::apply(bool close)
{
    if(!QDir::isAbsolutePath(capture_->text().trimmed())) { message_->setText(QStringLiteral("截图目录必须为绝对路径。")); return; }
    const QRegularExpression languages(QStringLiteral("^(?:[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})?)(?:,(?:[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})?))*$"));
    for(auto *edit:{audio_,sub_}) if(!edit->text().trimmed().isEmpty() && !languages.match(edit->text().trimmed()).hasMatch()) { message_->setText(QStringLiteral("语言请使用逗号分隔的语言代码，例如 eng,zho。")); return; }
    draft_.captureDirectory=capture_->text().trimmed(); draft_.audioLanguages=audio_->text().trimmed(); draft_.subtitleLanguages=sub_->text().trimmed(); draft_.recordHistory=history_->isChecked(); draft_.resumePolicy=resume_->currentIndex();
    if(!apply_(draft_,resetSubtitleScale_)) { message_->setText(QStringLiteral("保存失败，设置尚未应用。")); return; }
    resetSubtitleScale_=false;
    message_->setText(QStringLiteral("设置已应用。")); if(close) accept();
}
