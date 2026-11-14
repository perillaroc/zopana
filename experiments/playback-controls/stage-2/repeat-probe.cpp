#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlisthost.h"
#include "playlistpanel.h"
#include "settingsstore.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>

bool runPlaybackStageCase(const QString &mode, const QDir &fixtures, const QDir &output, QJsonObject &report);

namespace {
constexpr std::array modes{RepeatMode::None, RepeatMode::One, RepeatMode::All};
constexpr std::array repeatActions{PlayerActions::OrderSequential, PlayerActions::OrderSingle, PlayerActions::OrderList};

bool until(auto predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 6000) QTest::qWait(10);
    return predicate();
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("Could not initialize libmpv");
    return player;
}

class Probe
{
public:
    Probe(QDir fixtures, QDir output, QJsonObject &report)
        : window_(initialize(player_)), observer_(mpv_create_client(player_.handle(), "repeat-probe"), mpv_destroy),
          fixtures_(std::move(fixtures)), output_(std::move(output)), report_(report)
    {
        require(observer_ != nullptr, "mpv observer attached");
        player_.setMuted(true);
        window_.resize(1120, 720);
        window_.show();
        window_.raise();
        window_.activateWindow();
        require(until([&] { return video_->rendererReady() && window_.isActiveWindow(); }), "player window ready");
        report_["qt"] = qVersion();
        report_["dpr"] = video_->devicePixelRatioF();
        auto *version = mpv_get_property_string(observer_.get(), "mpv-version");
        report_["mpv"] = QString::fromUtf8(version);
        mpv_free(version);
    }

    void run()
    {
        checkMode(RepeatMode::None, false);
        shortcuts();
        for (const auto mode : modes) {
            for (const bool shuffle : {false, true}) {
                const auto name = QString("%1 / shuffle %2").arg(repeatAction(mode)->text(), shuffle ? "on" : "off");
                std::printf("CASE %s\n", name.toUtf8().constData());
                empty(mode, shuffle);
                singleton(mode, shuffle);
                threeItems(mode, shuffle);
            }
        }
        repeatChangeKeepsHistory();
    }

    void controls()
    {
        auto *panel = window_.findChild<PlaylistPanel *>();
        auto *host = window_.findChild<PlaylistHost *>();
        auto *repeat = panel->findChild<QToolButton *>("playlistRepeatButton");
        auto *shuffle = panel->findChild<QToolButton *>("playlistShuffleButton");
        auto *mainMenu = window_.findChild<QMenu *>("playerMenu");
        auto *play = window_.findChild<QMenu *>("playMenu");
        auto *context = panel->findChild<QMenu *>("playlistContextMenu");
        require(repeat && shuffle && mainMenu && play && context, "all playlist playback controls exist");
        auto *mainRepeat = play->findChild<QMenu *>("repeatModeMenu");
        auto *contextRepeat = context->findChild<QMenu *>("repeatModeMenu");
        require(mainRepeat && contextRepeat && repeat->defaultAction() == action(PlayerActions::RepeatCycle)
                    && shuffle->defaultAction() == action(PlayerActions::ShuffleToggle), "toolbar uses the shared playback actions");
        for (auto *menu : {mainRepeat, contextRepeat}) {
            const auto entries = menu->actions();
            require(entries.size() == 5 && entries[0] == repeatAction(RepeatMode::None)
                        && entries[1] == repeatAction(RepeatMode::One) && entries[2] == repeatAction(RepeatMode::All)
                        && entries[3]->isSeparator() && entries[4] == repeat->defaultAction(), "both menus use the same repeat choices");
        }
        require(play->actions().contains(shuffle->defaultAction()) && context->actions().contains(shuffle->defaultAction()),
                "both menus use the toolbar's shuffle action");

        const auto state = [&](RepeatMode mode, bool enabled) {
            checkMode(mode, enabled);
            const auto modeText = repeatAction(mode)->text();
            const auto shuffleText = enabled ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关");
            const auto title = QStringLiteral("循环模式（%1）").arg(modeText);
            require(repeat->text() == modeText && shuffle->text() == shuffleText && shuffle->isChecked() == enabled
                        && mainRepeat->title() == title && contextRepeat->title() == title,
                    "toolbar labels and submenu titles reflect playback state");
            require(repeat->isEnabled() && shuffle->isEnabled() && mainRepeat->isEnabled() && contextRepeat->isEnabled()
                        && repeatAction(mode)->isEnabled() && !repeat->accessibleName().isEmpty()
                        && repeat->accessibleDescription().contains(modeText) && shuffle->accessibleDescription().contains(shuffleText),
                    "controls are enabled and accessible with their current state");
        };
        const auto openContext = [&](QWidget *target) {
            const auto local = target->rect().center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, local, target->mapToGlobal(local));
            QApplication::sendEvent(target, &event);
            require(until([&] { return context->isVisible(); }), "playlist context menu opens from its visible content");
        };
        const auto screenshot = [&](QWidget *widget, const QString &name) {
            require(widget->grab().save(output_.filePath(name + ".png")), "saved " + name);
        };

        host->setVisible(true, false);
        require(until([&] { return panel->isVisible(); }), "docked empty playlist visible");
        const auto emptyGeneration = player_.generation();
        state(RepeatMode::None, false);
        QTest::mouseClick(repeat, Qt::LeftButton);
        state(RepeatMode::One, false);
        QTest::mouseClick(shuffle, Qt::LeftButton);
        state(RepeatMode::One, true);
        auto *empty = panel->findChild<QLabel *>("playlistEmpty");
        require(empty && empty->isVisible(), "empty playlist message visible");
        openContext(empty);
        clickMenu(context, shuffle->defaultAction());
        state(RepeatMode::One, false);
        require(queue_->model().rowCount() == 0 && player_.generation() == emptyGeneration, "empty controls submit no load");

        const auto ids = queue_->addFiles({fixtures_.filePath("E1.mp4"), fixtures_.filePath("E2.mp4")}, true);
        require(ids.size() == 2, "playlist populated");
        waitPaused(ids.first());
        panel->select(ids.last());
        const auto generation = player_.generation();
        const auto position = player_.position();

        auto *menuButton = window_.findChild<QToolButton *>("moreButton");
        require(menuButton && menuButton->menu() == mainMenu, "main menu button uses the playback menu's parent");
        openMainRepeat(menuButton, mainMenu, play, mainRepeat);
        screenshot(mainRepeat, "main-repeat-menu");
        clickMenu(mainRepeat, repeatAction(RepeatMode::All));
        state(RepeatMode::All, false);
        mainMenu->popup(menuButton->mapToGlobal(QPoint(0, menuButton->height())));
        openSubmenu(mainMenu, play);
        clickMenu(play, shuffle->defaultAction());
        state(RepeatMode::All, true);
        video_->setFocus();
        QTest::keyClick(video_, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier);
        state(RepeatMode::All, false);
        screenshot(panel, "docked");

        host->setDetached(true);
        auto *floating = host->window();
        floating->resize(floating->minimumSize());
        require(until([&] { return floating->isVisible() && panel->window() == floating; }), "independent playlist visible");
        requireControlsFit(panel, repeat, shuffle);
        QTest::mouseClick(repeat, Qt::LeftButton);
        state(RepeatMode::None, false);
        QTest::mouseClick(shuffle, Qt::LeftButton);
        state(RepeatMode::None, true);
        screenshot(floating, "independent-minimum");
        openContext(panel->view()->viewport());
        openSubmenu(context, contextRepeat);
        screenshot(contextRepeat, "context-repeat-menu");
        clickMenu(contextRepeat, repeatAction(RepeatMode::One));
        state(RepeatMode::One, true);

        host->setDetached(false);
        window_.resize(640, 400);
        require(until([&] { return host->overlayVisible(); }), "narrow playlist overlay visible");
        requireControlsFit(panel, repeat, shuffle);
        QTest::mouseClick(shuffle, Qt::LeftButton);
        state(RepeatMode::One, false);
        screenshot(panel, "narrow-overlay");

        window_.activateWindow();
        require(until([&] { return window_.isActiveWindow(); }), "main window active for fullscreen");
        action(PlayerActions::Fullscreen)->trigger();
        require(until([&] { return window_.isFullScreen(); }), "fullscreen entered");
        host->setVisible(true, false);
        require(until([&] { return host->overlayVisible(); }), "fullscreen playlist overlay visible");
        requireControlsFit(panel, repeat, shuffle);
        QTest::mouseClick(repeat, Qt::LeftButton);
        state(RepeatMode::All, false);
        screenshot(panel, "fullscreen-overlay");
        require(player_.generation() == generation && player_.itemId() == ids.first() && player_.paused()
                    && player_.position() == position && panel->selectedId() == ids.last(),
                "all controls and layouts preserve the paused item and selected row");
    }

private:
    void clickMenu(QMenu *menu, QAction *entry)
    {
        const auto rect = menu->actionGeometry(entry);
        require(menu->isVisible() && !rect.isEmpty() && entry->isEnabled(), "menu entry is available");
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, rect.center());
    }

    void openSubmenu(QMenu *parent, QMenu *child)
    {
        parent->setActiveAction(child->menuAction());
        QTest::keyClick(parent, Qt::Key_Right);
        require(until([&] { return child->isVisible(); }), "repeat submenu opens from its parent menu");
    }

    void openMainRepeat(QToolButton *button, QMenu *mainMenu, QMenu *play, QMenu *repeat)
    {
        mainMenu->popup(button->mapToGlobal(QPoint(0, button->height())));
        openSubmenu(mainMenu, play);
        openSubmenu(play, repeat);
    }

    void requireControlsFit(QWidget *panel, QToolButton *repeat, QToolButton *shuffle)
    {
        require(repeat->isVisible() && shuffle->isVisible() && panel->rect().contains(repeat->geometry())
                    && panel->rect().contains(shuffle->geometry()) && !repeat->geometry().intersects(shuffle->geometry())
                    && repeat->width() >= repeat->sizeHint().width() && shuffle->width() >= shuffle->sizeHint().width(),
                "both text buttons fit in the playlist panel");
    }

    void require(bool passed, const QString &name)
    {
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", name.toUtf8().constData());
        std::fflush(stdout);
        auto rows = report_["checks"].toArray();
        rows.append(QJsonObject{{"name", name}, {"passed", passed}});
        report_["checks"] = rows;
        if (!passed) throw std::runtime_error(name.toStdString());
    }

    QAction *action(PlayerActions::Id id) const { return actions_->get(id); }
    QAction *repeatAction(RepeatMode mode) const { return action(repeatActions[size_t(mode)]); }

    void checkMode(RepeatMode mode, bool shuffle)
    {
        bool checked = action(PlayerActions::ShuffleToggle)->isChecked() == shuffle;
        for (const auto candidate : modes) checked &= repeatAction(candidate)->isChecked() == (mode == candidate);
        require(checked && queue_->repeatMode() == mode && queue_->shuffleEnabled() == shuffle,
                "repeat and shuffle actions reflect the controller");
    }

    void selectMode(RepeatMode mode, bool shuffle)
    {
        repeatAction(mode)->trigger();
        if (queue_->shuffleEnabled() != shuffle) action(PlayerActions::ShuffleToggle)->trigger();
        checkMode(mode, shuffle);
    }

    void navigation(bool previous, bool next)
    {
        require(queue_->canPrevious() == previous && queue_->canNext() == next
                    && action(PlayerActions::Previous)->isEnabled() == previous
                    && action(PlayerActions::Next)->isEnabled() == next,
                QString("navigation availability previous=%1 next=%2").arg(previous).arg(next));
    }

    void waitPaused(quint64 id)
    {
        require(until([&] { return player_.state() == Player::State::Loaded && player_.itemId() == id && player_.canSeek(); }),
                "expected item loaded");
        player_.setPaused(true);
        require(until([&] { return player_.paused(); }), "item paused");
    }

    void noAdvance()
    {
        const auto generation = player_.generation();
        const auto current = queue_->model().currentId();
        if (!queue_->canNext()) queue_->next();
        if (!queue_->canPrevious()) queue_->previous();
        QTest::qWait(80);
        require(queue_->model().currentId() == current && player_.generation() == generation,
                "disabled directions submit no load");
    }

    void empty(RepeatMode mode, bool shuffle)
    {
        queue_->clear();
        selectMode(mode, shuffle);
        navigation(false, false);
        noAdvance();
    }

    quint64 finish()
    {
        const auto generation = player_.generation();
        const auto seek = QByteArray::number(player_.duration() - 0.3, 'f', 3);
        const char *args[]{"seek", seek.constData(), "absolute+exact", nullptr};
        require(mpv_command(observer_.get(), args) >= 0, "seek near natural EOF");
        require(until([&] { return std::abs(player_.position() - seek.toDouble()) < 0.1; }), "near EOF while paused");
        player_.setPaused(false);
        require(until([&] {
            return player_.generation() != generation || player_.state() == Player::State::Ended;
        }), "natural completion reached");
        QTest::qWait(80);
        if (player_.generation() == generation) {
            require(player_.state() == Player::State::Ended, "completion stops at the current item");
            return 0;
        }
        const auto id = player_.itemId();
        waitPaused(id);
        require(player_.generation() == generation + 1, "completion starts exactly one load");
        return id;
    }

    void singleton(RepeatMode mode, bool shuffle)
    {
        const auto ids = queue_->addFiles({fixtures_.filePath("E1.mp4")}, true);
        require(ids.size() == 1, "one-item queue populated");
        waitPaused(ids.first());
        navigation(!shuffle && mode == RepeatMode::All, mode == RepeatMode::All);
        const auto expected = mode == RepeatMode::None ? 0 : ids.first();
        require(finish() == expected, "one-item natural completion matches mode");
        noAdvance();
        if (mode == RepeatMode::All) {
            const auto before = player_.generation();
            action(PlayerActions::Next)->trigger();
            waitPaused(ids.first());
            require(player_.generation() == before + 1, "one-item repeat-all wraps manually");
        }
    }

    void threeItems(RepeatMode mode, bool shuffle)
    {
        const auto ids = queue_->addFiles({fixtures_.filePath("E1.mp4"), fixtures_.filePath("E2.mp4"),
                                           fixtures_.filePath("E10.mp4")}, true);
        require(ids.size() == 3, "three-item queue populated");
        waitPaused(ids.first());
        navigation(!shuffle && mode == RepeatMode::All, true);
        noAdvance();
        if (mode == RepeatMode::One) {
            require(finish() == ids.first(), "single repeat at start");
            action(PlayerActions::Next)->trigger();
        } else {
            require(ids.mid(1).contains(finish()), "automatic next leaves the first item");
        }
        const auto second = queue_->model().currentId();
        require(shuffle ? ids.mid(1).contains(second) : second == ids[1], "second item follows the selected order");
        waitPaused(second);
        if (mode == RepeatMode::One) action(PlayerActions::Next)->trigger();
        else require(ids.contains(finish()), "automatic next advances within the round");
        const auto third = queue_->model().currentId();
        require(shuffle ? ids.contains(third) && third != second && third != ids.first() : third == ids.last(),
                "third item finishes the round without a repeat");
        waitPaused(third);
        navigation(true, mode == RepeatMode::All);
        noAdvance();

        const auto afterRound = finish();
        if (mode == RepeatMode::All) {
            require(shuffle ? ids.contains(afterRound) && afterRound != third : afterRound == ids.first(),
                    "repeat-all starts a new round");
            action(PlayerActions::Previous)->trigger();
            waitPaused(third);
        } else {
            require(afterRound == (mode == RepeatMode::One ? third : 0), "only repeat-all advances beyond the round");
        }
        action(PlayerActions::Previous)->trigger();
        waitPaused(second);
        navigation(true, true);
        action(PlayerActions::Next)->trigger();
        waitPaused(third);

        if (mode == RepeatMode::One) {
            queue_->play(second);
            waitPaused(second);
            require(finish() == second, "single repeat follows an explicit new selection");
        }
        if (mode == RepeatMode::All) {
            action(PlayerActions::Next)->trigger();
            const auto wrapped = queue_->model().currentId();
            require(shuffle ? ids.contains(wrapped) && wrapped != third : wrapped == ids.first(),
                    "repeat-all manually advances beyond the round");
            waitPaused(wrapped);
        }
    }

    void repeatChangeKeepsHistory()
    {
        selectMode(RepeatMode::None, true);
        const auto ids = queue_->addFiles({fixtures_.filePath("E1.mp4"), fixtures_.filePath("E2.mp4"),
                                           fixtures_.filePath("E10.mp4")}, true);
        require(ids.size() == 3, "shuffle history fixture loaded");
        waitPaused(ids.first());
        action(PlayerActions::Next)->trigger();
        const auto second = queue_->model().currentId();
        waitPaused(second);
        action(PlayerActions::Previous)->trigger();
        waitPaused(ids.first());
        const auto generation = player_.generation();
        action(PlayerActions::RepeatCycle)->trigger();
        checkMode(RepeatMode::One, true);
        require(player_.generation() == generation && player_.paused(), "repeat change keeps the paused shuffle session");
        require(finish() == ids.first(), "single repeat keeps the current history position");
        require(finish() == ids.first(), "repeating the current item leaves forward history intact");
        action(PlayerActions::Next)->trigger();
        waitPaused(second);
        require(second != ids.first(), "manual next reuses the recorded shuffle item");
        action(PlayerActions::ShuffleToggle)->trigger();
        checkMode(RepeatMode::One, false);
        action(PlayerActions::ShuffleToggle)->trigger();
        checkMode(RepeatMode::One, true);
        navigation(false, true);
        noAdvance();
    }

    void key(Qt::KeyboardModifiers modifiers, QWidget *target = nullptr)
    {
        auto *widget = target ? target : video_;
        widget->setFocus();
        QTest::keyClick(widget, Qt::Key_R, modifiers);
    }

    void shortcuts()
    {
        require(!action(PlayerActions::RepeatCycle)->autoRepeat() && !action(PlayerActions::ShuffleToggle)->autoRepeat(),
                "both new shortcuts are single-press commands");
        const auto *playMenu = window_.findChild<QMenu *>("playMenu");
        bool hasCycle = false;
        bool legacyAbsent = !playMenu->actions().contains(action(PlayerActions::OrderRandom));
        for (auto *entry : playMenu->actions()) {
            if (const auto *menu = entry->menu()) {
                hasCycle |= menu->actions().contains(action(PlayerActions::RepeatCycle));
                legacyAbsent &= !menu->actions().contains(action(PlayerActions::OrderRandom));
            }
        }
        require(legacyAbsent, "legacy random is absent from playback menus");
        require(hasCycle && playMenu->actions().contains(action(PlayerActions::ShuffleToggle)), "playback menu exposes both new commands");
        for (const auto mode : {RepeatMode::One, RepeatMode::All, RepeatMode::None}) {
            key(Qt::ControlModifier);
            checkMode(mode, false);
        }
        key(Qt::ControlModifier | Qt::ShiftModifier);
        checkMode(RepeatMode::None, true);
        key(Qt::ControlModifier | Qt::ShiftModifier);
        checkMode(RepeatMode::None, false);
        auto *host = window_.findChild<PlaylistHost *>();
        auto *view = window_.findChild<PlaylistPanel *>()->view();
        host->setVisible(true, false);
        require(until([&] { return view->isVisible(); }), "playlist panel shown");
        view->setFocus();
        require(view->hasFocus(), "playlist has keyboard focus");
        key(Qt::ControlModifier, view);
        key(Qt::ControlModifier | Qt::ShiftModifier, view);
        checkMode(RepeatMode::None, false);
        host->setVisible(false, false);
        video_->setFocus();

        const SettingsStore settings(output_.filePath("legacy-config"));
        require(QDir().mkpath(output_.filePath("legacy-config")), "isolated legacy settings directory");
        QSettings ini(settings.fileName(), QSettings::IniFormat);
        ini.setValue("schemaVersion", 1);
        ini.setValue("shortcuts/queue.order.sequential", "F6");
        ini.setValue("shortcuts/queue.order.single", "F7");
        ini.setValue("shortcuts/queue.order.list", "Ctrl+Shift+R");
        ini.setValue("shortcuts/queue.order.random", "Ctrl+R");
        ini.sync();
        require(ini.status() == QSettings::NoError, "legacy shortcut fixture written");
        SettingsStore reader(output_.filePath("legacy-config"));
        const auto ignored = actions_->applyBindings(reader.load().shortcuts);
        require(ignored.contains("queue.repeat.cycle") && ignored.contains("queue.shuffle.toggle")
                    && action(PlayerActions::RepeatCycle)->shortcuts().isEmpty()
                    && action(PlayerActions::ShuffleToggle)->shortcuts().isEmpty(),
                "legacy bindings take precedence over both new defaults");
        key(Qt::ControlModifier);
        checkMode(RepeatMode::All, true);
        QTest::keyClick(video_, Qt::Key_F6);
        checkMode(RepeatMode::None, true);
        QTest::keyClick(video_, Qt::Key_F7);
        checkMode(RepeatMode::One, true);
        key(Qt::ControlModifier | Qt::ShiftModifier);
        checkMode(RepeatMode::All, true);
        action(PlayerActions::RepeatCycle)->trigger();
        checkMode(RepeatMode::None, true);
        action(PlayerActions::ShuffleToggle)->trigger();
        checkMode(RepeatMode::None, false);

        bool unassignedInHelp = false;
        QTimer::singleShot(0, &window_, [&] {
            auto *dialog = window_.findChild<QDialog *>("shortcutsDialog");
            if (!dialog) { if (auto *modal = QApplication::activeModalWidget()) modal->close(); return; }
            auto *table = dialog->findChild<QTableWidget *>("shortcutsTable");
            unassignedInHelp = table && table->item(PlayerActions::RepeatCycle, 1)->text() == QStringLiteral("未分配")
                && table->item(PlayerActions::ShuffleToggle, 1)->text() == QStringLiteral("未分配");
            dialog->reject();
        });
        action(PlayerActions::Shortcuts)->trigger();
        require(unassignedInHelp, "F1 reports displaced default shortcuts as unassigned");
        actions_->applyBindings({});
    }

    Player player_;
    MainWindow window_;
    std::unique_ptr<mpv_handle, decltype(&mpv_destroy)> observer_;
    const QDir fixtures_, output_;
    QJsonObject &report_;
    PlaylistController *queue_ = window_.findChild<PlaylistController *>();
    PlayerActions *actions_ = window_.findChild<PlayerActions *>();
    VideoWidget *video_ = window_.findChild<VideoWidget *>();
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("zopana repeat probe");
    const auto args = app.arguments();
    if (args.size() != 3 && args.size() != 4) return 2;
    const auto mode = args.value(3);
    const QDir output(args[2]);
    QJsonObject report;
    bool passed = false;
    try {
        if (mode.isEmpty() || mode == "--controls") {
            Probe probe(QDir(args[1]), output, report);
            if (mode == "--controls") probe.controls();
            else probe.run();
        } else if (!runPlaybackStageCase(mode, QDir(args[1]), output, report)) {
            return 2;
        }
        passed = true;
    } catch (const std::exception &error) {
        report["failure"] = QString::fromUtf8(error.what());
    }
    report["passed"] = passed;
    QSaveFile evidence(output.filePath("results.json"));
    if (!evidence.open(QIODevice::WriteOnly)) return 2;
    const auto json = QJsonDocument(report).toJson();
    if (evidence.write(json) != json.size() || !evidence.commit()) return 2;
    return passed ? 0 : 1;
}
