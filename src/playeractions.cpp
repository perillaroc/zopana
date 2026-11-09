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
    {"window.licenses", "第三方许可", "file-text"}
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
        window.addAction(action);
        actions_[i] = action;
    }
    get(Open)->setShortcuts(QKeySequence::Open);
    get(PlayPause)->setShortcut(QKeySequence(Qt::Key_Space));
    get(ResetView)->setShortcut(QKeySequence(Qt::Key_R));
    get(Fullscreen)->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    get(Escape)->setShortcut(QKeySequence(Qt::Key_Escape));
    auto *mode = new QActionGroup(this);
    for (auto id : {FeatureMode, StandardMode}) { get(id)->setCheckable(true); mode->addAction(get(id)); }
    get(FeatureMode)->setChecked(true);
    get(FeatureMode)->setStatusTip(QStringLiteral("视频区滚轮缩放，按住左键拖动画面"));
    get(StandardMode)->setStatusTip(QStringLiteral("视频区滚轮调音量，单击播放或暂停"));
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) { if (now != composing_) composing_.clear(); });
    refreshIcons();
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
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        const bool viewKey = modifiers == Qt::NoModifier
            && (key->key() == Qt::Key_Space || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_R);
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
    get(ResetView)->setStatusTip(hasVideo ? QStringLiteral("恢复完整画面并居中") : QStringLiteral("当前没有可复位的视频画面"));
    get(Retry)->setEnabled(retryable);
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
