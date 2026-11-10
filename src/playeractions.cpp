#include "playeractions.h"
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
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
    {"seek.backSmall", "后退 5 秒", ""}, {"seek.forwardSmall", "前进 5 秒", ""},
    {"seek.backMedium", "后退 30 秒", ""}, {"seek.forwardMedium", "前进 30 秒", ""},
    {"seek.backLarge", "后退 60 秒", ""}, {"seek.forwardLarge", "前进 60 秒", ""},
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
    {"queue.order.sequential", "顺序播放", ""}, {"queue.order.single", "单曲循环", ""},
    {"queue.order.list", "列表循环", ""}, {"queue.order.random", "随机播放", ""}
};
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
        action->setShortcutContext(Qt::WindowShortcut);
        action->setAutoRepeat(false);
        action->setShortcutVisibleInContextMenu(true);
        if (i != ClosePlaylist && !(i >= QueuePlay && i <= QueueDown)) window.addAction(action);
        actions_[i] = action;
        action->setProperty("scope", QStringLiteral("窗口"));
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
    get(ClosePlaylist)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W)); get(ClosePlaylist)->setProperty("scope", QStringLiteral("独立列表"));
    for (auto id : {QueuePlay, QueueRemove, QueueUp, QueueDown}) { get(id)->setShortcutContext(Qt::WidgetShortcut); get(id)->setProperty("scope", QStringLiteral("队列")); }
    get(QueuePlay)->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    get(QueueRemove)->setShortcut(QKeySequence(Qt::Key_Delete));
    get(QueueUp)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up)); get(QueueDown)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Down));
    auto *orders = new QActionGroup(this);
    for (auto id : {OrderSequential, OrderSingle, OrderList, OrderRandom}) { get(id)->setCheckable(true); orders->addAction(get(id)); }
    get(OrderSequential)->setChecked(true);
    get(Fullscreen)->setProperty("scope", QStringLiteral("窗口：F11；观看：Enter"));
    auto *mode = new QActionGroup(this);
    for (auto id : {FeatureMode, StandardMode}) { get(id)->setCheckable(true); mode->addAction(get(id)); }
    get(FeatureMode)->setChecked(true);
    get(FeatureMode)->setStatusTip(QStringLiteral("视频区滚轮缩放，按住左键拖动画面"));
    get(StandardMode)->setStatusTip(QStringLiteral("视频区滚轮调音量，单击播放或暂停"));
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) { if (now != composing_) composing_.clear(); });
    refreshIcons();
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
    return QApplication::activeWindow() == &window_ && !QApplication::activePopupWidget()
        && !QApplication::activeModalWidget() && !composing_
        && (!focus || focus == &video_ || focus == &surface_ || focus == &window_);
}

bool PlayerActions::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::InputMethod) {
        const auto *input = static_cast<QInputMethodEvent *>(event);
        if (input->preeditString().isEmpty()) { if (composing_ == object) composing_.clear(); }
        else composing_ = object;
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
        const QKeySequence pressed(QKeyCombination(modifiers, Qt::Key(key->key())));
        bool viewKey = modifiers == Qt::NoModifier && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        for (auto *action : actions_) {
            if (action->property("scope").toString() != QStringLiteral("观看")) continue;
            for (const auto &sequence : action->shortcuts()) viewKey |= sequence.matches(pressed) == QKeySequence::ExactMatch;
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
    get(ResetView)->setStatusTip(hasVideo ? QStringLiteral("恢复完整画面并居中") : QStringLiteral("当前没有可复位的视频画面"));
    get(Retry)->setEnabled(retryable);
    refreshIcons();
}

void PlayerActions::setSeekable(bool seekable)
{
    for (auto id : {BackSmall, ForwardSmall, BackMedium, ForwardMedium, BackLarge, ForwardLarge, SeekStart, SeekEnd, SeekGoto})
    { get(id)->setEnabled(seekable); get(id)->setStatusTip(seekable ? get(id)->text() : QStringLiteral("当前媒体尚不可定位")); }
    refreshIcons();
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
    for (auto *action : actions_) {
        const auto name = action->property("iconName").toString();
        if (!name.isEmpty()) action->setIcon(Theme::icon(name, window_.palette()));
        QStringList keys;
        for (const auto &key : action->shortcuts()) keys.append(key.toString(QKeySequence::NativeText));
        const auto tip = action->statusTip().isEmpty() ? action->text() : action->statusTip();
        action->setToolTip(keys.isEmpty() ? tip : tip + QStringLiteral("（%1）").arg(keys.join(" / ")));
    }
}
