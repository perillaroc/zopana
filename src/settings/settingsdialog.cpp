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

SettingsDialog::SettingsDialog(const Preferences &preferences, PlayerActions &actions,
                               std::function<bool(const Preferences &, const SettingsEdits &)> apply,
                               std::function<void()> clear, QWidget *parent)
    : QDialog(parent), draft_(preferences), actions_(actions), apply_(std::move(apply))
{
    setObjectName("settingsDialog"); setWindowTitle(QStringLiteral("设置")); resize(760,620);
    auto *layout=new QVBoxLayout(this); tabs_=new QTabWidget; tabs_->setObjectName("settingsTabs"); layout->addWidget(tabs_);
    general_=new QWidget; auto *form=new QFormLayout(general_); tabs_->addTab(general_,QStringLiteral("播放与记录"));
    capture_=new QLineEdit(draft_.captureDirectory); capture_->setObjectName("captureDirectoryEdit"); capture_->setAccessibleName(QStringLiteral("截图目录"));
    auto *folder=new QWidget; auto *row=new QHBoxLayout(folder); row->setContentsMargins(0,0,0,0); row->addWidget(capture_); auto *browse=new QPushButton(QStringLiteral("浏览…")); row->addWidget(browse);
    connect(browse,&QPushButton::clicked,this,[this] { const auto path=QFileDialog::getExistingDirectory(this,QStringLiteral("选择截图目录"),capture_->text()); if(!path.isEmpty()) capture_->setText(path); }); form->addRow(QStringLiteral("截图目录"),folder);
    audio_=new QLineEdit(draft_.audioLanguages); audio_->setObjectName("audioLanguagesEdit"); sub_=new QLineEdit(draft_.subtitleLanguages); sub_->setObjectName("subtitleLanguagesEdit");
    for(auto *edit:{audio_,sub_}) edit->setPlaceholderText(QStringLiteral("例如 eng,zho；留空使用媒体默认"));
    form->addRow(QStringLiteral("优先音轨语言"),audio_); form->addRow(QStringLiteral("优先字幕语言"),sub_);
    auto *languageNote=new QLabel(QStringLiteral("语言偏好按顺序用于下次打开媒体；当前轨道仍可从菜单手动选择。")); languageNote->setWordWrap(true); form->addRow(languageNote);
    history_=new QCheckBox(QStringLiteral("记录最近文件和播放进度")); history_->setObjectName("historyEnabledCheck"); history_->setChecked(draft_.recordHistory); form->addRow(history_);
    resume_=new QComboBox; resume_->setObjectName("resumePolicyCombo"); resume_->addItems({QStringLiteral("询问是否继续"),QStringLiteral("总是继续有效断点"),QStringLiteral("始终从头播放")}); resume_->setCurrentIndex(draft_.resumePolicy); form->addRow(QStringLiteral("断点策略"),resume_);
    connect(capture_,&QLineEdit::textChanged,this,[this] { edits_.captureDirectory=true; });
    connect(audio_,&QLineEdit::textChanged,this,[this] { edits_.audioLanguages=true; });
    connect(sub_,&QLineEdit::textChanged,this,[this] { edits_.subtitleLanguages=true; });
    connect(history_,&QCheckBox::toggled,this,[this] { edits_.recordHistory=true; });
    connect(resume_,&QComboBox::currentIndexChanged,this,[this] { edits_.resumePolicy=true; });
    auto *historyNote=new QLabel(QStringLiteral("仅在播放超过 30 秒且距离结尾超过 30 秒时恢复。关闭记录保留旧记录；清除立即生效，并暂停当前文件的记录，直到再次显式打开。")); historyNote->setWordWrap(true); form->addRow(historyNote);
    auto *clearButton=new QPushButton(QStringLiteral("立即清除最近文件和进度")); clearButton->setObjectName("clearHistoryButton"); form->addRow(clearButton);
    connect(clearButton,&QPushButton::clicked,this,[this,clear] { clear(); message_->setText(QStringLiteral("最近文件和进度已清除；此操作已立即生效。")); });
    auto *shortcuts=new QWidget; auto *keysLayout=new QVBoxLayout(shortcuts); tabs_->addTab(shortcuts,QStringLiteral("快捷键"));
    filter_=new QLineEdit; filter_->setObjectName("shortcutFilter"); filter_->setPlaceholderText(QStringLiteral("过滤分类、命令或快捷键")); keysLayout->addWidget(filter_);
    table_=new QTableWidget(0,3); table_->setObjectName("shortcutTable"); table_->setHorizontalHeaderLabels({QStringLiteral("分类"),QStringLiteral("操作"),QStringLiteral("快捷键")}); table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->horizontalHeader()->setStretchLastSection(true); table_->verticalHeader()->hide(); keysLayout->addWidget(table_);
    auto *record=new QHBoxLayout; first_=new QKeySequenceEdit; first_->setObjectName("shortcutFirst"); second_=new QKeySequenceEdit; second_->setObjectName("shortcutSecond");
    for(auto *edit:{first_,second_}) { edit->setMaximumSequenceLength(1); edit->setAccessibleName(edit==first_?QStringLiteral("第一组合键"):QStringLiteral("第二组合键")); record->addWidget(edit); }
    auto *set=new QPushButton(QStringLiteral("设置绑定")); set->setObjectName("setBindingButton"); record->addWidget(set); keysLayout->addLayout(record); connect(set,&QPushButton::clicked,this,&SettingsDialog::bind);
    auto *resetRow=new QHBoxLayout; auto *empty=new QPushButton(QStringLiteral("清空此项")),*reset=new QPushButton(QStringLiteral("恢复此项"));
    resetRow->addWidget(empty); resetRow->addWidget(reset); keysLayout->addLayout(resetRow);
    const auto editable=[this]() -> QString { const auto *item=table_->item(table_->currentRow(),1); return item?item->data(Qt::UserRole).toString():QString(); };
    connect(empty,&QPushButton::clicked,this,[this,editable] { const auto id=editable(); if(!id.isEmpty() && actions_.bindingError(id,{}).isEmpty()) { draft_.shortcuts[id]=QStringList(); edits_.shortcuts.insert(id); populate(); } });
    connect(reset,&QPushButton::clicked,this,[this,editable] { const auto id=editable(); if(!id.isEmpty()) { draft_.shortcuts.remove(id); edits_.shortcuts.insert(id); populate(); } });
    connect(table_,&QTableWidget::currentCellChanged,this,[this] { select(); }); connect(filter_,&QLineEdit::textChanged,this,[this] { populate(); });
    auto *note=new QLabel(QStringLiteral("点击录制框后按组合键，再设置绑定。F11/Enter 全屏、Esc 和 Tab 保持固定；取消不应用草稿。")); note->setWordWrap(true); keysLayout->addWidget(note);
    message_=new QLabel; message_->setObjectName("settingsMessage"); message_->setWordWrap(true); layout->addWidget(message_);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Apply|QDialogButtonBox::RestoreDefaults); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定")); buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消")); buttons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("应用")); buttons->button(QDialogButtonBox::RestoreDefaults)->setText(QStringLiteral("恢复本页默认"));
    auto *all = buttons->addButton(QStringLiteral("恢复全部设置"), QDialogButtonBox::ResetRole);
    all->setObjectName("resetAllSettingsButton");
    connect(buttons,&QDialogButtonBox::accepted,this,[this] { this->apply(true); }); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this] { this->apply(false); });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &SettingsDialog::resetPage);
    connect(all, &QPushButton::clicked, this, &SettingsDialog::resetAll);
    populate();
}

void SettingsDialog::resetPage()
{
    if (tabs_->currentWidget() == general_) {
        const auto defaults = SettingsStore::defaults();
        capture_->setText(defaults.captureDirectory);
        audio_->setText(defaults.audioLanguages);
        sub_->setText(defaults.subtitleLanguages);
        history_->setChecked(defaults.recordHistory);
        resume_->setCurrentIndex(defaults.resumePolicy);
        edits_.captureDirectory=true;
        edits_.audioLanguages=true;
        edits_.subtitleLanguages=true;
        edits_.recordHistory=true;
        edits_.resumePolicy=true;
    } else {
        resetShortcuts();
    }
    message_->setText(QStringLiteral("本页已恢复默认，点击应用或确定后生效。"));
}

void SettingsDialog::resetShortcuts()
{
    for (const auto &id : actions_.defaultBindings().keys()) {
        draft_.shortcuts.remove(id);
        edits_.shortcuts.insert(id);
    }
    populate();
}

void SettingsDialog::resetAll()
{
    QMessageBox confirmation(QMessageBox::Question, QStringLiteral("恢复全部设置"),
        QStringLiteral("将恢复全部偏好和快捷键。播放历史、媒体文件和已保存的截图会保留。"),
        QMessageBox::Cancel, this);
    auto *reset = confirmation.addButton(QStringLiteral("恢复全部设置"), QMessageBox::AcceptRole);
    confirmation.exec();
    if (confirmation.clickedButton() != reset) return;
    edits_.resetAll = true;
    tabs_->setCurrentWidget(general_);
    resetPage();
    resetShortcuts();
    message_->setText(QStringLiteral("全部设置已恢复默认，点击应用或确定后生效。"));
}
void SettingsDialog::populate()
{
    const auto *old=table_->item(table_->currentRow(),1); const auto selected=old?old->data(Qt::UserRole).toString():QString();
    const auto bindings=actions_.resolvedBindings(draft_.shortcuts);
    const QSignalBlocker blocker(table_); table_->setRowCount(0); int selectRow=0;
    for(int i=0;i<PlayerActions::Count;++i) {
        auto *action=actions_.get(PlayerActions::Id(i)); const auto id=action->objectName(); const auto keys=bindings.value(id).toStringList();
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
    const auto *item=table_->item(table_->currentRow(),1); if(!item) return; const auto id=item->data(Qt::UserRole).toString(); const auto keys=actions_.resolvedBindings(draft_.shortcuts).value(id).toStringList();
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
        const auto bindings=actions_.resolvedBindings(draft_.shortcuts);
        for(const auto &other:conflicts) { auto previous=bindings.value(other).toStringList(); previous.removeIf([&](const QString &old) { for(const auto &key:keys) if(PlayerActions::normalizedKey(key)==PlayerActions::normalizedKey(old)) return true; return false; }); draft_.shortcuts[other]=previous; edits_.shortcuts.insert(other); }
    }
    draft_.shortcuts[id]=keys; edits_.shortcuts.insert(id); message_->setText(QStringLiteral("绑定已加入草稿，应用后生效。")); populate();
}
void SettingsDialog::apply(bool close)
{
    if(edits_.captureDirectory && !QDir::isAbsolutePath(capture_->text().trimmed())) { message_->setText(QStringLiteral("截图目录必须为绝对路径。")); return; }
    const QRegularExpression languages(QStringLiteral("^(?:[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})?)(?:,(?:[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8})?))*$"));
    const auto validLanguages=[&languages](const QLineEdit *edit) {
        const auto text=edit->text().trimmed();
        return text.isEmpty() || languages.match(text).hasMatch();
    };
    if((edits_.audioLanguages && !validLanguages(audio_)) || (edits_.subtitleLanguages && !validLanguages(sub_))) {
        message_->setText(QStringLiteral("语言请使用逗号分隔的语言代码，例如 eng,zho。"));
        return;
    }
    draft_.captureDirectory=capture_->text().trimmed(); draft_.audioLanguages=audio_->text().trimmed(); draft_.subtitleLanguages=sub_->text().trimmed(); draft_.recordHistory=history_->isChecked(); draft_.resumePolicy=resume_->currentIndex();
    if(!apply_(draft_, edits_)) { message_->setText(QStringLiteral("保存失败，设置尚未应用。")); return; }
    edits_={};
    message_->setText(QStringLiteral("设置已应用。")); if(close) accept();
}
