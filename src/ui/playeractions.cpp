#include "playeractions.h"
#include "settingsstore.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSet>
#include <QWidget>

namespace {
struct Definition { const char *id; const char *text; const char *icon; };
constexpr Definition definitions[] = {
    {"file.open", "打开文件", "folder-open"}, {"file.retry", "重试", "refresh-cw"},
    {"play.toggle", "播放", "play"}, {"view.reset", "恢复适窗", "scan"},
    {"window.fullscreen", "全屏", "maximize"}, {"window.escape", "返回", ""},
    {"view.mode.feature", "缩放拖动", "move"}, {"view.mode.standard", "常规播放", "mouse-pointer-2"},
    {"app.quit", "退出", ""}, {"window.about", "关于 zopana", "info"},
    {"window.licenses", "第三方许可", "file-text"},
    {"play.stop", "停止", "square"},
    {"seek.backSmall", "后退", "skip-back"}, {"seek.forwardSmall", "前进", "skip-forward"},
    {"seek.backMedium", "后退", ""}, {"seek.forwardMedium", "前进", ""},
    {"seek.backLarge", "后退", ""}, {"seek.forwardLarge", "前进", ""},
    {"seek.start", "跳到开头", ""}, {"seek.end", "跳到最后画面", ""},
    {"seek.goto", "跳转时间…", ""},
    {"audio.volumeUp", "增大音量", ""}, {"audio.volumeDown", "减小音量", ""},
    {"audio.mute", "静音", "volume-2"}, {"view.inputMode", "切换鼠标模式", ""},
    {"view.zoomIn", "放大画面", "zoom-in"}, {"view.zoomOut", "缩小画面", "zoom-out"},
    {"window.shortcuts", "快捷键帮助", "keyboard"},
    {"file.add", "添加文件", "list-plus"}, {"file.addFolder", "添加文件夹", "folder-plus"}, {"file.cancelScan", "取消扫描", ""},
    {"play.previous", "上一项", "skip-back"}, {"play.next", "下一项", "skip-forward"},
    {"window.playlist", "播放列表", "list-video"}, {"window.playlistMode", "独立窗口", ""}, {"window.closePlaylist", "隐藏列表窗口", ""},
    {"queue.playSelected", "播放所选", "play"}, {"queue.removeSelected", "移除所选", ""},
    {"queue.moveUp", "上移", ""}, {"queue.moveDown", "下移", ""}, {"queue.clear", "清空列表", ""},
    {"queue.order.sequential", "不循环", ""}, {"queue.order.single", "单个循环", ""},
    {"queue.order.list", "全部循环", ""}, {"queue.order.random", "旧版随机播放（开启全部循环和随机）", ""},
    {"queue.repeat.cycle", "切换循环模式", ""}, {"queue.shuffle.toggle", "随机播放", ""},
    {"audio.nextTrack","下一音轨",""}, {"audio.delayLess","音频提前 0.1 秒",""}, {"audio.delayMore","音频推迟 0.1 秒",""}, {"audio.delayReset","音频延迟归零",""},
    {"sub.load","加载外挂字幕…","captions"}, {"sub.toggle","显示字幕",""}, {"sub.nextTrack","下一字幕轨",""},
    {"sub.delayLess","字幕提前 0.1 秒",""}, {"sub.delayMore","字幕推迟 0.1 秒",""}, {"sub.delayReset","字幕延迟归零",""},
    {"sub.scaleUp","字幕放大 0.1",""}, {"sub.scaleDown","字幕缩小 0.1",""}, {"sub.scaleReset","字幕缩放恢复 1.0",""},
    {"rate.decrease","减速 0.25×",""}, {"rate.increase","加速 0.25×",""}, {"rate.reset","1.0×",""},
    {"rate.preset.050","0.5×",""}, {"rate.preset.075","0.75×",""}, {"rate.preset.125","1.25×",""},
    {"rate.preset.150","1.5×",""}, {"rate.preset.200","2.0×",""}, {"rate.preset.300","3.0×",""}, {"audio.pitchCorrection","保持音调",""},
    {"frame.forward","暂停并前进一帧","step-forward"}, {"loop.setA","设置 A 点",""}, {"loop.setB","设置 B 点",""}, {"loop.clear","清除 A–B",""},
    {"capture.source","完整画面截图","camera"}, {"capture.viewport","当前取景截图","camera"}, {"capture.openFolder","打开截图文件夹","folder-open"},
    {"window.mediaInfo","媒体信息","info"}, {"file.location","打开文件位置","folder-open"}, {"window.alwaysOnTop","窗口置顶","pin"},
    {"window.settings","设置",""}, {"file.clearHistory","清除最近文件和进度",""},
    {"play.skip.toggle","启用跳略播放",""}, {"play.skip.intro","跳过片头",""},
    {"play.skip.outro","跳过片尾",""}, {"play.skip.settings","跳略设置…",""},
    {"view.rotate.next","顺时针旋转 90°",""}, {"view.rotate.previous","逆时针旋转 90°",""}, {"view.rotate.reset","复位额外旋转",""},
    {"window.scale.050","窗口 50%",""}, {"window.scale.100","窗口 100%",""}, {"window.scale.150","窗口 150%",""},
    {"window.scale.200","窗口 200%",""}, {"window.scale.fit","窗口适合工作区",""},
    {"queue.sort.fileName.asc","文件名",""}, {"queue.sort.title.asc","媒体标题",""}, {"queue.sort.extension.asc","扩展名",""},
    {"queue.sort.folder.asc","所在文件夹",""}, {"queue.sort.path.asc","完整路径",""}, {"queue.sort.fileSize.asc","文件大小",""},
    {"queue.sort.modifiedTime.asc","修改时间",""}, {"queue.sort.createdTime.asc","创建时间",""}, {"queue.sort.duration.asc","时长",""},
    {"queue.sort.resolution.asc","分辨率",""}, {"queue.sort.addedOrder.asc","添加顺序",""},
    {"queue.sort.fileName.desc","文件名",""}, {"queue.sort.title.desc","媒体标题",""}, {"queue.sort.extension.desc","扩展名",""},
    {"queue.sort.folder.desc","所在文件夹",""}, {"queue.sort.path.desc","完整路径",""}, {"queue.sort.fileSize.desc","文件大小",""},
    {"queue.sort.modifiedTime.desc","修改时间",""}, {"queue.sort.createdTime.desc","创建时间",""}, {"queue.sort.duration.desc","时长",""},
    {"queue.sort.resolution.desc","分辨率",""}, {"queue.sort.addedOrder.desc","添加顺序",""},
    {"queue.sort.again","按上次条件重新排序",""}, {"queue.sort.reverse","反转当前顺序",""},
    {"queue.sort.shuffle","随机打乱列表",""}, {"queue.sort.cancel","取消排序",""},
    {"window.controls", "控制窗口", "sliders-horizontal"},
    {"chapter.previous", "上一章", ""}, {"chapter.next", "下一章", ""},
    {"seek.undo", "撤销上次定位", ""},
    {"file.openPlaylist", "打开片单…", ""}, {"file.appendPlaylist", "追加片单…", ""},
    {"file.exportPlaylist", "导出片单…", ""}
};
constexpr PlayerActions::Id seekPairs[][2] = {
    {PlayerActions::BackSmall, PlayerActions::ForwardSmall},
    {PlayerActions::BackMedium, PlayerActions::ForwardMedium},
    {PlayerActions::BackLarge, PlayerActions::ForwardLarge}
};
constexpr std::array repeatActions{PlayerActions::OrderSequential, PlayerActions::OrderSingle, PlayerActions::OrderList};
constexpr int sortFields = int(SortField::AddedOrder) + 1;
static_assert(PlayerActions::SortFileNameDesc - PlayerActions::SortFileNameAsc == sortFields);
static_assert(PlayerActions::SortAgain - PlayerActions::SortFileNameDesc == sortFields);

PlayerActions::Id sortAction(PlaylistSort sort)
{
    const auto base = sort.direction == SortDirection::Ascending ? PlayerActions::SortFileNameAsc : PlayerActions::SortFileNameDesc;
    return PlayerActions::Id(base + int(sort.field));
}

QString directionText(SortDirection direction)
{
    return direction == SortDirection::Ascending ? QStringLiteral("升序") : QStringLiteral("降序");
}

bool timelineNavigationKey(QKeyCombination key)
{
    if (key.keyboardModifiers() & ~Qt::KeypadModifier) return false;
    return key.key() == Qt::Key_Home || key.key() == Qt::Key_End
        || key.key() == Qt::Key_PageUp || key.key() == Qt::Key_PageDown;
}
}

PlayerActions::PlayerActions(QWidget &window, QWidget &video, QWidget &surface)
    : QObject(&window), window_(window), video_(video), surface_(surface)
{
    static_assert(std::size(definitions) == Count);
    for (int i = 0; i < Count; ++i) {
        const auto &definition = definitions[i];
        auto *action = new QAction(QString::fromUtf8(definition.text), this);
        action->setObjectName(QString::fromLatin1(definition.id));
        action->setProperty("iconName", definition.icon);
        const auto sortCommand = i >= SortFileNameAsc && i <= CancelSort;
        action->setShortcutContext(sortCommand ? Qt::WidgetShortcut : Qt::WindowShortcut);
        action->setAutoRepeat(false);
        action->setShortcutVisibleInContextMenu(true);
        if (i != ClosePlaylist && !(i >= QueuePlay && i <= QueueDown) && !sortCommand) window.addAction(action);
        actions_[i] = action;
        action->setProperty("scope", sortCommand ? QStringLiteral("队列") : QStringLiteral("窗口"));
        connect(action, &QAction::triggered, this, &PlayerActions::invoked);
    }
    get(Open)->setShortcuts(QKeySequence::Open);
    get(PlayPause)->setShortcut(QKeySequence(Qt::Key_Space));
    get(ResetView)->setShortcut(QKeySequence(Qt::Key_R));
    get(Fullscreen)->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    get(Escape)->setShortcut(QKeySequence(Qt::Key_Escape));
    get(Escape)->setProperty("scope", QStringLiteral("当前层"));
    const auto view = [this](Id id, QList<QKeySequence> keys, bool repeat = false) {
        get(id)->setShortcuts(keys); get(id)->setAutoRepeat(repeat);
        get(id)->setProperty("scope", QStringLiteral("观看"));
    };
    view(PlayPause, {QKeySequence(Qt::Key_Space)});
    view(ResetView, {QKeySequence(Qt::Key_R)});
    view(Stop, {QKeySequence(Qt::Key_S)});
    view(BackSmall, {QKeySequence(Qt::Key_Left)}, true); view(ForwardSmall, {QKeySequence(Qt::Key_Right)}, true);
    view(BackMedium, {QKeySequence(Qt::SHIFT | Qt::Key_Left)}, true); view(ForwardMedium, {QKeySequence(Qt::SHIFT | Qt::Key_Right)}, true);
    view(BackLarge, {QKeySequence(Qt::CTRL | Qt::Key_Left)}, true); view(ForwardLarge, {QKeySequence(Qt::CTRL | Qt::Key_Right)}, true);
    for (size_t step = 0; step < seekSteps_.size(); ++step) {
        connect(get(seekPairs[step][0]), &QAction::triggered, this, [this, step] { emit seekRelativeRequested(-seekSteps_[step], continuingSeekKey_); });
        connect(get(seekPairs[step][1]), &QAction::triggered, this, [this, step] { emit seekRelativeRequested(seekSteps_[step], continuingSeekKey_); });
    }
    const Preferences defaults;
    setSeekSteps(defaults.seekSmallSeconds, defaults.seekMediumSeconds, defaults.seekLargeSeconds);
    view(SeekStart, {QKeySequence(Qt::Key_Home)}); view(SeekEnd, {QKeySequence(Qt::Key_End)});
    get(SeekGoto)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    view(VolumeUp, {QKeySequence(Qt::Key_Up)}, true); view(VolumeDown, {QKeySequence(Qt::Key_Down)}, true);
    view(Mute, {QKeySequence(Qt::Key_M)}); get(Mute)->setCheckable(true);
    view(InputMode, {QKeySequence(Qt::Key_V)});
    view(ZoomIn, {QKeySequence(Qt::Key_Equal), QKeySequence(Qt::Key_Plus), QKeySequence(Qt::SHIFT | Qt::Key_Plus)}, true);
    view(ZoomOut, {QKeySequence(Qt::Key_Minus)}, true);
    get(Shortcuts)->setShortcut(QKeySequence(Qt::Key_F1));
    get(Add)->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    view(Previous, {QKeySequence(Qt::Key_PageUp)}); view(Next, {QKeySequence(Qt::Key_PageDown)});
    view(Playlist, {QKeySequence(Qt::Key_P)}); get(Playlist)->setCheckable(true);
    get(ControlWindowToggle)->setShortcut(QKeySequence(Qt::Key_F7));
    get(ControlWindowToggle)->setCheckable(true);
    get(ClosePlaylist)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W)); get(ClosePlaylist)->setProperty("scope", QStringLiteral("独立列表"));
    for (auto id : {QueuePlay, QueueRemove, QueueUp, QueueDown}) { get(id)->setShortcutContext(Qt::WidgetShortcut); get(id)->setProperty("scope", QStringLiteral("队列")); }
    get(QueuePlay)->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    get(QueueRemove)->setShortcut(QKeySequence(Qt::Key_Delete));
    get(QueueUp)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up)); get(QueueDown)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down));
    auto *repeat = new QActionGroup(this);
    for (auto id : repeatActions) { get(id)->setCheckable(true); repeat->addAction(get(id)); }
    view(RepeatCycle, {QKeySequence(Qt::CTRL | Qt::Key_R)});
    view(ShuffleToggle, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)});
    get(ShuffleToggle)->setCheckable(true);
    setPlaybackOrderState(RepeatMode::None, false);
    auto *sort = new QActionGroup(this);
    for (auto direction : {SortDirection::Ascending, SortDirection::Descending}) {
        for (int field = 0; field < sortFields; ++field) {
            const PlaylistSort choice{SortField(field), direction};
            auto *action = get(sortAction(choice));
            action->setCheckable(true);
            action->setText(QStringLiteral("%1（%2）").arg(action->text(), directionText(direction)));
            sort->addAction(action);
            connect(action, &QAction::triggered, this, [this, choice] { emit playlistSortRequested(choice); });
        }
    }
    get(CancelSort)->setEnabled(false);
    view(AudioNext,{QKeySequence(Qt::Key_J)});
    view(AudioDelayLess,{QKeySequence(Qt::SHIFT|Qt::Key_Comma),QKeySequence(Qt::SHIFT|Qt::Key_Less)},true);
    view(AudioDelayMore,{QKeySequence(Qt::SHIFT|Qt::Key_Period),QKeySequence(Qt::SHIFT|Qt::Key_Greater)},true);
    get(SubLoad)->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_L));
    view(SubToggle,{QKeySequence(Qt::Key_H)}); get(SubToggle)->setCheckable(true);
    view(SubNext,{QKeySequence(Qt::Key_K)});
    view(SubDelayLess,{QKeySequence(Qt::Key_Comma)},true); view(SubDelayMore,{QKeySequence(Qt::Key_Period)},true);
    view(SubScaleUp,{QKeySequence(Qt::CTRL|Qt::Key_Equal),QKeySequence(Qt::CTRL|Qt::Key_Plus),QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_Plus)},true);
    view(SubScaleDown,{QKeySequence(Qt::CTRL|Qt::Key_Minus)},true); view(SubScaleReset,{QKeySequence(Qt::CTRL|Qt::Key_0)});
    view(RateLess,{QKeySequence(Qt::Key_BracketLeft)},true); view(RateMore,{QKeySequence(Qt::Key_BracketRight)},true);
    view(RateReset,{QKeySequence(Qt::Key_Backspace)});
    for (auto id : {RateReset,RateHalf,RateThreeQuarter,Rate125,Rate150,Rate200,Rate300,Pitch}) get(id)->setCheckable(true);
    view(FrameForward,{QKeySequence(Qt::Key_D)}); view(LoopA,{QKeySequence(Qt::Key_A)}); view(LoopB,{QKeySequence(Qt::Key_B)}); view(LoopClear,{QKeySequence(Qt::CTRL|Qt::Key_B)});
    get(CaptureSource)->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_S)); get(CaptureViewport)->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_S));
    view(MediaInfo,{QKeySequence(Qt::Key_I)}); view(AlwaysOnTop,{QKeySequence(Qt::Key_T)}); get(AlwaysOnTop)->setCheckable(true);
    get(Settings)->setShortcut(QKeySequence(Qt::Key_F5));
    get(Settings)->setEnabled(false); get(ClearHistory)->setEnabled(false);
    view(SkipToggle, {QKeySequence(Qt::SHIFT | Qt::Key_Apostrophe)});
    view(SkipSettings, {QKeySequence(Qt::Key_Apostrophe)});
    for (auto id : {SkipToggle, SkipIntro, SkipOutro}) get(id)->setCheckable(true);
    for (auto id : {SkipToggle, SkipIntro, SkipOutro, SkipSettings}) get(id)->setEnabled(false);
    view(RotateNext, {QKeySequence(Qt::ALT | Qt::Key_K)});
    view(RotatePrevious, {QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_K)});
    view(RotateReset, {QKeySequence(Qt::ALT | Qt::CTRL | Qt::Key_K)});
    for (int i = 0; i < 5; ++i) {
        const auto id = Id(Scale50 + i);
        const auto key = Qt::Key(Qt::Key_1 + i);
        view(id, {QKeySequence(key), QKeySequence(Qt::KeypadModifier | key)});
    }
    get(Fullscreen)->setProperty("scope", QStringLiteral("窗口：F11；观看：Enter"));
    auto *mode = new QActionGroup(this);
    for (auto id : {FeatureMode, StandardMode}) { get(id)->setCheckable(true); mode->addAction(get(id)); }
    get(FeatureMode)->setChecked(true);
    get(FeatureMode)->setStatusTip(QStringLiteral("视频区滚轮缩放，按住左键拖动画面"));
    get(StandardMode)->setStatusTip(QStringLiteral("视频区滚轮调音量，单击播放或暂停"));
    for(auto *action:actions_) { QStringList keys; for(const auto &key:action->shortcuts()) keys.append(key.toString(QKeySequence::PortableText)); defaults_[action->objectName()]=keys; }
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now != composing_) composing_.clear();
        seekKey_ = 0;
    });
    refreshIcons();
}

QAction *PlayerActions::repeatModeAction(RepeatMode mode) const
{
    return get(repeatActions[static_cast<size_t>(mode)]);
}

void PlayerActions::addPlaybackOrderActions(QMenu &menu) const
{
    auto *repeat = menu.addMenu(QStringLiteral("循环模式"));
    repeat->setObjectName("repeatModeMenu");
    for (auto id : repeatActions) repeat->addAction(get(id));
    repeat->addSeparator();
    auto *cycle = get(RepeatCycle);
    repeat->addAction(cycle);
    const auto updateTitle = [repeat, cycle] {
        repeat->setTitle(QStringLiteral("循环模式（%1）").arg(cycle->iconText()));
    };
    connect(cycle, &QAction::changed, repeat, updateTitle);
    updateTitle();
    menu.addAction(get(ShuffleToggle));
}

QString PlayerActions::sortLabel(std::optional<PlaylistSort> sort)
{
    if (!sort) return QStringLiteral("手动顺序");
    return QStringLiteral("%1 · %2").arg(QString::fromUtf8(definitions[sortAction(*sort)].text), directionText(sort->direction));
}

QMenu *PlayerActions::playlistSortMenu(QWidget *parent) const
{
    auto *menu = new QMenu(QStringLiteral("列表排序"), parent);
    menu->setObjectName("playlistSortMenu");
    for (auto direction : {SortDirection::Ascending, SortDirection::Descending}) {
        auto *group = menu->addMenu(directionText(direction));
        for (int field = 0; field < sortFields; ++field) group->addAction(get(sortAction({SortField(field), direction})));
    }
    menu->addSeparator();
    for (auto id : {SortAgain, ReverseQueue, ShuffleQueue, CancelSort}) menu->addAction(get(id));
    return menu;
}

void PlayerActions::setPlaylistSortState(PlaylistSort last, int count)
{
    get(sortAction(last))->setChecked(true);
    get(SortAgain)->setText(QStringLiteral("按上次条件重新排序（%1）").arg(sortLabel(last)));
    for (int id = SortFileNameAsc; id <= SortAgain; ++id) actions_[id]->setEnabled(count > 0);
    for (auto id : {ReverseQueue, ShuffleQueue}) get(id)->setEnabled(count > 1);
}

void PlayerActions::setPlaybackOrderState(RepeatMode mode, bool shuffleEnabled)
{
    auto *selected = repeatModeAction(mode);
    selected->setChecked(true);
    auto *cycle = get(RepeatCycle);
    cycle->setIconText(selected->text());
    cycle->setStatusTip(QStringLiteral("切换循环模式，当前为%1").arg(selected->text()));
    auto *shuffle = get(ShuffleToggle);
    shuffle->setChecked(shuffleEnabled);
    shuffle->setIconText(shuffleEnabled ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关"));
    shuffle->setStatusTip(shuffle->iconText());
}

namespace {
QKeyCombination normalized(const QKeySequence &sequence)
{
    auto key=sequence[0].key(); auto modifiers=sequence[0].keyboardModifiers() & ~Qt::KeypadModifier;
    if(key==Qt::Key_Plus) { key=Qt::Key_Equal; modifiers|=Qt::ShiftModifier; }
    if(key==Qt::Key_Less) { key=Qt::Key_Comma; modifiers|=Qt::ShiftModifier; }
    if(key==Qt::Key_Greater) { key=Qt::Key_Period; modifiers|=Qt::ShiftModifier; }
    if(key==Qt::Key_QuoteDbl) { key=Qt::Key_Apostrophe; modifiers|=Qt::ShiftModifier; }
    if(key==Qt::Key_Enter) key=Qt::Key_Return;
    return QKeyCombination(modifiers,key);
}
}
QString PlayerActions::normalizedKey(const QString &key) { return QKeySequence(normalized(QKeySequence::fromString(key,QKeySequence::PortableText))).toString(QKeySequence::PortableText); }

bool PlayerActions::viewingInputAllowed() const
{
    return QApplication::activeWindow() == &window_ && !composing_
        && !QApplication::activePopupWidget() && !QApplication::activeModalWidget();
}

QAction *PlayerActions::timelineAction(const QKeyEvent &event) const
{
    if (timelineNavigationKey(event.keyCombination())) {
        if (event.key() == Qt::Key_Home) return get(SeekStart);
        if (event.key() == Qt::Key_End) return get(SeekEnd);
        return nullptr;
    }
    return relativeSeekAction(event);
}

QAction *PlayerActions::relativeSeekAction(const QKeyEvent &event) const
{
    const auto pressed = normalized(QKeySequence(event.keyCombination()));
    for (const auto &pair : seekPairs) {
        for (auto id : pair) {
            for (const auto &key : get(id)->shortcuts()) {
                if (normalized(key) == pressed) return get(id);
            }
        }
    }
    return nullptr;
}

bool PlayerActions::continuesSeekKey(const QKeyEvent &event)
{
    const bool continuing = event.isAutoRepeat() && seekKey_ == event.key();
    seekKey_ = event.key();
    return continuing;
}

void PlayerActions::triggerSeekKey(QAction &action, const QKeyEvent &event)
{
    if (!action.isEnabled() || (event.isAutoRepeat() && !action.autoRepeat())) return;
    const QScopedValueRollback repeat(continuingSeekKey_, continuesSeekKey(event));
    action.trigger();
}

void PlayerActions::seekTimelineStep(int seconds, const QKeyEvent &event)
{
    const bool continuing = continuesSeekKey(event);
    emit invoked();
    emit seekRelativeRequested(seconds, continuing);
}

QString PlayerActions::timelineDescription() const
{
    QStringList commands;
    for (const auto &pair : seekPairs) {
        for (auto id : pair) {
            const auto *action = get(id);
            QStringList keys;
            for (const auto &key : action->shortcuts()) {
                if (!timelineNavigationKey(key[0])) keys.append(key.toString(QKeySequence::NativeText));
            }
            if (!keys.isEmpty()) commands.append(QStringLiteral("%1（%2）").arg(action->text(), keys.join(" / ")));
        }
    }
    commands.append(QStringLiteral("PageUp / PageDown 每次定位 10 秒"));
    commands.append(QStringLiteral("Home / End 跳到开头或结尾"));
    return commands.join('\n');
}

QString PlayerActions::bindingError(const QString &id,const QStringList &keys) const
{
    QAction *action=nullptr; for(auto *a:actions_) if(a->objectName()==id) action=a;
    if(!action) return QStringLiteral("未知命令");
    if(action==get(Fullscreen) || action==get(Escape)) return QStringLiteral("F11、Enter 和 Esc 保留为全屏及退出兜底");
    if(keys.size()>2 && keys!=defaults_.value(id).toStringList()) return QStringLiteral("每个命令最多两个组合键");
    for(const auto &text:keys) {
        const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText);
        if(sequence.count()!=1 || sequence.isEmpty()) return QStringLiteral("只支持单个有效组合键");
        const auto combination=normalized(sequence); const auto key=combination.key(); const auto modifiers=combination.keyboardModifiers();
        if(key==Qt::Key_unknown || key==Qt::Key_Control || key==Qt::Key_Shift || key==Qt::Key_Alt || key==Qt::Key_Meta || key==Qt::Key_AltGr || key==Qt::Key_Escape || key==Qt::Key_Tab || key==Qt::Key_Backtab || key==Qt::Key_F11
            || modifiers.testFlag(Qt::MetaModifier) || (modifiers.testFlag(Qt::AltModifier) && (key==Qt::Key_F4 || key==Qt::Key_Space)) || (modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::AltModifier) && key==Qt::Key_Delete)) return QStringLiteral("此按键由系统或焦点导航保留");
        if(action->property("scope").toString()==QStringLiteral("窗口")) {
            const bool platformAlias=defaults_.value(id).toStringList().contains(text);
            if(modifiers==Qt::NoModifier && !(key>=Qt::Key_F1 && key<=Qt::Key_F35) && !platformAlias) return QStringLiteral("窗口命令请使用功能键或带修饰键的组合");
            if(modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::AltModifier) && (key==Qt::Key_A || key==Qt::Key_C || key==Qt::Key_V || key==Qt::Key_X || key==Qt::Key_Y || key==Qt::Key_Z || key==Qt::Key_Left || key==Qt::Key_Right || key==Qt::Key_Home || key==Qt::Key_End)) return QStringLiteral("此组合保留给原生编辑操作");
        }
    }
    return {};
}
QStringList PlayerActions::conflicts(const QString &id,const QStringList &keys,const QVariantMap &overrides) const
{
    return bindingConflicts(id, keys, resolvedBindings(overrides));
}

QStringList PlayerActions::bindingConflicts(const QString &id, const QStringList &keys, const QVariantMap &bindings) const
{
    if (keys.isEmpty()) return {};
    QAction *selected=nullptr; for(auto *a:actions_) if(a->objectName()==id) selected=a;
    if(!selected) return {};
    QSet<QString> normalizedKeys;
    for(const auto &key:keys) normalizedKeys.insert(normalizedKey(key));
    QStringList result; const auto scope=selected->property("scope").toString();
    for(auto *action:actions_) {
        if(action==selected) continue; const auto other=action->property("scope").toString();
        if((scope==QStringLiteral("观看") && other==QStringLiteral("队列")) || (scope==QStringLiteral("队列") && other==QStringLiteral("观看"))) continue;
        for(const auto &binding:bindings.value(action->objectName()).toStringList()) {
            if(normalizedKeys.contains(normalizedKey(binding))) { result.append(action->objectName()); break; }
        }
    }
    return result;
}
QVariantMap PlayerActions::resolvedBindings(const QVariantMap &overrides, QStringList *ignored) const
{
    // Protected shortcuts always win. Process non-default customizations first
    // so legacy rows that repeat their defaults cannot displace them.
    const auto fullscreen=get(Fullscreen)->objectName(), escape=get(Escape)->objectName();
    QVariantMap bindings{{fullscreen,defaults_.value(fullscreen)},{escape,defaults_.value(escape)}};
    const auto queuePlay=get(QueuePlay)->objectName();
    bindings.insert(queuePlay, defaults_.value(queuePlay));
    QVariantMap custom, overridesMatchingDefaults;
    const auto report=[ignored](const QString &id) { if(ignored && !ignored->contains(id)) ignored->append(id); };
    for(auto it=overrides.cbegin();it!=overrides.cend();++it) {
        const auto id=it.key();
        if(!defaults_.contains(id)) continue;
        const auto type=it.value().metaType().id();
        const auto keys=it.value().toStringList();
        // QSettings serializes an empty QStringList as @Invalid() in INI files.
        if((it.value().isValid() && type!=QMetaType::QString && type!=QMetaType::QStringList) || !bindingError(id,keys).isEmpty()
            || !bindingConflicts(id,keys,bindings).isEmpty()) {
            report(id);
        } else if(keys!=defaults_.value(id).toStringList()) {
            custom.insert(id,keys);
        } else {
            overridesMatchingDefaults.insert(id,keys);
        }
    }
    bindings.remove(queuePlay);
    for(auto it=custom.cbegin();it!=custom.cend();++it) {
        if(bindingConflicts(it.key(),it.value().toStringList(),custom).isEmpty()) bindings.insert(it.key(),it.value());
        else report(it.key());
    }
    for(auto it=overridesMatchingDefaults.cbegin(); it!=overridesMatchingDefaults.cend(); ++it) {
        if(bindingConflicts(it.key(),it.value().toStringList(),bindings).isEmpty()) bindings.insert(it.key(),it.value());
        else report(it.key());
    }
    for(auto *action:actions_) {
        const auto id=action->objectName();
        if(bindings.contains(id)) continue;
        if(id==queuePlay) {
            // The queue's default Enter/Return operates on list focus; the
            // same keys enter fullscreen in the viewing area.
            bindings.insert(id, defaults_.value(id));
            continue;
        }
        const auto defaults=defaults_.value(id).toStringList();
        auto keys=defaults;
        keys.removeIf([&](const QString &key) { return !bindingConflicts(id,{key},bindings).isEmpty(); });
        if(keys!=defaults) report(id);
        bindings.insert(id,keys);
    }
    return bindings;
}

QStringList PlayerActions::applyBindings(const QVariantMap &overrides)
{
    QStringList ignored;
    const auto bindings=resolvedBindings(overrides,&ignored);
    for(auto *action:actions_) {
        QList<QKeySequence> keys; for(const auto &text:bindings.value(action->objectName()).toStringList()) { const auto sequence=QKeySequence::fromString(text,QKeySequence::PortableText); if(!keys.contains(sequence)) keys.append(sequence); }
        action->setShortcuts(keys);
    }
    refreshIcons(); emit bindingsChanged(); return ignored;
}

void PlayerActions::attachPlaylist(QWidget &window, QWidget &view)
{
    playlistWindow_ = &window; queue_ = &view;
    for (auto *action : actions_) {
        const auto scope = action->property("scope").toString();
        if (scope == QStringLiteral("队列")) view.addAction(action);
        else if (scope != QStringLiteral("观看") && action != get(Quit)) window.addAction(action);
    }
}

bool PlayerActions::watching() const
{
    auto *focus = QApplication::focusWidget();
    return viewingInputAllowed() && (!focus || focus == &video_ || focus == &surface_ || focus == &window_);
}

bool PlayerActions::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::InputMethod) {
        const auto *input = static_cast<QInputMethodEvent *>(event);
        if (input->preeditString().isEmpty()) { if (composing_ == object) composing_.clear(); }
        else composing_ = object;
    }
    if (event->type() == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (!key->isAutoRepeat() && key->key() == seekKey_) seekKey_ = 0;
    } else if (object == &window_ && event->type() == QEvent::WindowDeactivate) {
        seekKey_ = 0;
    }
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && watching()) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (auto *action = relativeSeekAction(*key)) {
            key->accept();
            if (event->type() == QEvent::KeyPress) triggerSeekKey(*action, *key);
            return true;
        }
    }
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && queue_
        && QApplication::focusWidget() == queue_) {
        auto *key = static_cast<QKeyEvent *>(event);
        const QKeySequence pressed(QKeyCombination(key->modifiers() & ~Qt::KeypadModifier, Qt::Key(key->key())));
        for (auto id : {QueuePlay, QueueRemove, QueueUp, QueueDown}) for (const auto &sequence : get(id)->shortcuts()) {
            if (sequence.matches(pressed) != QKeySequence::ExactMatch) continue;
            key->accept();
            const bool allowed = !QApplication::activeModalWidget() && !QApplication::activePopupWidget() && !composing_
                && (QApplication::activeWindow() == &window_ || QApplication::activeWindow() == playlistWindow_);
            if (event->type() == QEvent::KeyPress && !allowed) return QObject::eventFilter(object, event);
            if (event->type() == QEvent::KeyPress && allowed && !key->isAutoRepeat() && get(id)->isEnabled()) get(id)->trigger();
            return true;
        }
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        const QKeySequence pressed = normalized(QKeySequence(QKeyCombination(modifiers, Qt::Key(key->key()))));
        bool viewKey = modifiers == Qt::NoModifier && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        for (auto *action : actions_) {
            if (action->property("scope").toString() != QStringLiteral("观看")) continue;
            for (const auto &sequence : action->shortcuts()) viewKey |= QKeySequence(normalized(sequence)).matches(pressed) == QKeySequence::ExactMatch;
        }
        if (viewKey && !watching()) { key->accept(); return true; }
        if (key->key() == Qt::Key_Escape && (QApplication::activePopupWidget() || QApplication::activeModalWidget())) {
            key->accept(); return true;
        }
    }
    return QObject::eventFilter(object, event);
}

void PlayerActions::setMediaState(bool loaded, bool paused, bool replayable, bool hasVideo, bool retryable)
{
    get(PlayPause)->setEnabled(loaded || replayable);
    get(PlayPause)->setText(loaded && !paused ? QStringLiteral("暂停") : replayable ? QStringLiteral("重新播放") : QStringLiteral("播放"));
    get(PlayPause)->setProperty("iconName", loaded && !paused ? "pause" : "play");
    get(ResetView)->setEnabled(hasVideo);
    get(Stop)->setEnabled(loaded);
    for (auto id : {ZoomIn, ZoomOut}) get(id)->setEnabled(hasVideo);
    for (auto id : {RotateNext, RotatePrevious, RotateReset, Scale50, Scale100, Scale150, Scale200, ScaleFit}) get(id)->setEnabled(hasVideo);
    get(ResetView)->setStatusTip(hasVideo ? QStringLiteral("恢复完整画面并居中") : QStringLiteral("当前没有可复位的视频画面"));
    get(Retry)->setEnabled(retryable);
    refreshIcons();
}

void PlayerActions::setSeekable(bool seekable)
{
    if (get(SeekGoto)->isEnabled() == seekable) return;
    for (auto id : {BackSmall, ForwardSmall, BackMedium, ForwardMedium, BackLarge, ForwardLarge, SeekStart, SeekEnd, SeekGoto}) {
        auto *action = get(id);
        action->setStatusTip(seekable ? action->text() : QStringLiteral("当前媒体尚不可定位"));
        action->setEnabled(seekable);
    }
    refreshIcons();
}

void PlayerActions::setSeekSteps(int small, int medium, int large)
{
    const std::array steps{small, medium, large};
    if (seekSteps_ == steps) return;
    seekSteps_ = steps;
    for (size_t step = 0; step < seekSteps_.size(); ++step) {
        for (auto id : seekPairs[step]) {
            auto *action = get(id);
            const auto name = id == seekPairs[step][0] ? QStringLiteral("后退 %1 秒") : QStringLiteral("前进 %1 秒");
            action->setText(name.arg(seekSteps_[step]));
            if (action->isEnabled()) action->setStatusTip(action->text());
        }
    }
    refreshIcons();
    emit seekStepsChanged();
}

void PlayerActions::setAudioAvailable(bool available)
{
    for (auto id : {VolumeUp, VolumeDown, Mute}) {
        get(id)->setEnabled(available); get(id)->setStatusTip(available ? QString() : QStringLiteral("当前媒体没有可用音轨"));
    }
    refreshIcons();
}

void PlayerActions::setMuted(bool muted)
{
    get(Mute)->setChecked(muted);
    get(Mute)->setProperty("iconName", muted ? "volume-x" : "volume-2");
    get(Mute)->setText(muted ? QStringLiteral("取消静音") : QStringLiteral("静音"));
    refreshIcons();
}

void PlayerActions::setFullscreen(bool fullscreen)
{
    get(Fullscreen)->setText(fullscreen ? QStringLiteral("退出全屏") : QStringLiteral("全屏"));
    get(Fullscreen)->setProperty("iconName", fullscreen ? "minimize" : "maximize");
    get(Fullscreen)->setCheckable(true);
    get(Fullscreen)->setChecked(fullscreen);
    refreshIcons();
}

void PlayerActions::refreshIcons()
{
    const auto &palette = window_.palette();
    const bool paletteChanged = iconPaletteKey_ != palette.cacheKey();
    iconPaletteKey_ = palette.cacheKey();
    for (size_t index = 0; index < actions_.size(); ++index) {
        auto *action = actions_[index];
        const auto name = action->property("iconName").toString();
        if (!name.isEmpty() && (paletteChanged || appliedIconNames_[index] != name)) {
            action->setIcon(Theme::icon(name, palette));
            appliedIconNames_[index] = name;
        }
        QStringList keys;
        for (const auto &key : action->shortcuts()) keys.append(key.toString(QKeySequence::NativeText));
        const auto tip = action->statusTip().isEmpty() ? action->text() : action->statusTip();
        action->setToolTip(keys.isEmpty() ? tip : tip + QStringLiteral("（%1）").arg(keys.join(" / ")));
    }
}
