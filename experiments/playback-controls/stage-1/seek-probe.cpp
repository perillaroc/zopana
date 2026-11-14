#include "mainwindow.h"
#include "playeractions.h"
#include "playlistcontroller.h"
#include "playlistpanel.h"
#include "playlisthost.h"
#include "sessioncontroller.h"
#include "videowidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool result, const char *description)
{
    if (!result) throw std::runtime_error(description);
    std::printf("PASS %s\n", description);
}

bool until(auto predicate, int timeout = 6000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout) QTest::qWait(10);
    return predicate();
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < .08;
}

Player &initialize(Player &player)
{
    if (!player.initialize()) throw std::runtime_error("initialize libmpv");
    return player;
}

struct Probe {
    SettingsStore settings;
    HistoryStore history;
    Player player;
    MainWindow window;
    SessionController session;
    PlayerActions *actions = window.findChild<PlayerActions *>();
    PlaylistController *playlist = window.findChild<PlaylistController *>();
    VideoWidget *video = window.findChild<VideoWidget *>("videoWidget");
    QSlider *slider = window.findChild<QSlider *>("seekSlider");
    QToolButton *back = window.findChild<QToolButton *>("backButton");
    QToolButton *forward = window.findChild<QToolButton *>("forwardButton");
    QSignalSpy requests{&player, &Player::seekRequested};
    QDir media, output;

    Probe(const QString &profile, const QString &fixtures)
        : settings(profile + "/config"), history(profile + "/data"), window(initialize(player)),
          session(player, window, settings, history), media(fixtures), output(profile)
    {
        window.show();
        activate();
        require(until([&] { return video->rendererReady(); }), "OpenGL widget is ready");
        std::printf("Qt %s, DPR %.2f\n", qVersion(), video->devicePixelRatioF());
        player.setMuted(true);
    }

    void activate()
    {
        window.raise(); window.activateWindow();
        require(QTest::qWaitForWindowActive(&window), "main window active");
    }

    void waitItem(quint64 id)
    {
        require(until([&] { return player.itemId() == id && player.canSeek() && slider->isEnabled(); }), "playlist item is loaded and seekable");
        player.setPaused(true);
        require(until([&] { return player.paused(); }), "pause is confirmed");
    }

    QList<quint64> openQueue()
    {
        const auto ids = playlist->addFiles({media.filePath("long-grid.mp4"), media.filePath("audio.m4a")}, true);
        require(ids.size() == 2, "open two owned fixtures");
        waitItem(ids.front());
        return ids;
    }

    bool disabled() const
    {
        return !slider->isEnabled() && !actions->get(PlayerActions::BackSmall)->isEnabled()
            && !actions->get(PlayerActions::ForwardSmall)->isEnabled() && !actions->get(PlayerActions::BackMedium)->isEnabled()
            && !actions->get(PlayerActions::ForwardMedium)->isEnabled() && !actions->get(PlayerActions::BackLarge)->isEnabled()
            && !actions->get(PlayerActions::ForwardLarge)->isEnabled() && !actions->get(PlayerActions::SeekStart)->isEnabled()
            && !actions->get(PlayerActions::SeekEnd)->isEnabled() && !actions->get(PlayerActions::SeekGoto)->isEnabled();
    }

    void position(double target)
    {
        require(until([&] { return near(player.position(), target) && near(player.positionTarget(), target) && player.paused(); }),
            "decoded position and target settle while paused");
        QTest::qWait(70);
    }

    void seek(double target)
    {
        player.seek(target);
        position(target);
    }

    void keySeek(QWidget *focus, Qt::Key key, Qt::KeyboardModifiers mods, double target)
    {
        focus->setFocus(Qt::TabFocusReason);
        QTest::qWait(10);
        require(focus->hasFocus(), "expected widget owns keyboard focus");
        requests.clear();
        QTest::keyClick(focus, key, mods);
        require(requests.size() == 1 && near(requests.first().at(0).toDouble(), target), "key submits one expected seek target");
        position(target);
    }

    void buttonSeek(QToolButton *button, double delta)
    {
        const double target = player.positionTarget() + delta;
        requests.clear();
        QTest::mouseClick(button, Qt::LeftButton);
        require(requests.size() == 1 && near(requests.first().at(0).toDouble(), target), "left mouse button submits the current small step");
        position(target);
    }

    void defaults()
    {
        for (auto *focus : {static_cast<QWidget *>(video), static_cast<QWidget *>(slider)}) {
            for (auto [modifiers, seconds] : {std::pair{Qt::NoModifier, 5}, {Qt::ShiftModifier, 30}, {Qt::ControlModifier, 60}}) {
                seek(70);
                keySeek(focus, Qt::Key_Left, modifiers, 70 - seconds);
                keySeek(focus, Qt::Key_Right, modifiers, 70);
            }
        }
        seek(20);
        buttonSeek(forward, 5);
        buttonSeek(back, -5);
        keySeek(slider, Qt::Key_PageUp, Qt::NoModifier, 30);
        keySeek(slider, Qt::Key_PageDown, Qt::NoModifier, 20);
        keySeek(slider, Qt::Key_End, Qt::NoModifier, player.lastPosition());
        keySeek(slider, Qt::Key_Home, Qt::NoModifier, 0);
    }

    void repeat(QWidget *focus, Qt::Key key, Qt::KeyboardModifiers mods)
    {
        focus->setFocus();
        for (int i = 0; i < 10; ++i) {
            QKeyEvent event(QEvent::KeyPress, key, mods, {}, true);
            QApplication::sendEvent(focus, &event);
        }
    }

    void coalescing()
    {
        seek(20);
        requests.clear();
        QSignalSpy commands(&player, &Player::requestSubmitted);
        repeat(slider, Qt::Key_Right, Qt::NoModifier);
        require(requests.size() == 10 && near(player.positionTarget(), 70), "ten auto-repeat inputs accumulate from 20 to 70 seconds");
        position(70);
        int engineSeeks = 0;
        for (const auto &event : commands) engineSeeks += event.at(3).toString().startsWith("seek ");
        require(engineSeeks == 1, "coalesced input sends one engine seek");

        seek(1);
        requests.clear();
        repeat(video, Qt::Key_Left, Qt::NoModifier);
        require(requests.size() == 10 && player.positionTarget() == 0, "repeated viewing keys clamp at the start");
        position(0);
        seek(135);
        requests.clear();
        repeat(slider, Qt::Key_Right, Qt::ControlModifier);
        require(requests.size() == 10 && player.positionTarget() == player.lastPosition(), "repeated timeline keys clamp at the final frame");
        position(player.lastPosition());
        QSignalSpy noMoreLoads(&player, &Player::mediaChanged);
        QTest::qWait(200);
        require(player.state() == Player::State::Loaded && player.paused() && noMoreLoads.isEmpty(), "tail seeking stays paused without advancing the queue");
    }

    void focusSafety()
    {
        seek(20);
        QLineEdit text(&window);
        text.setGeometry(20, 80, 200, 32);
        text.show(); text.setFocus();
        const auto normal = window.geometry();
        requests.clear();
        QTest::keyClicks(&text, "12345");
        QTest::keyClick(&text, Qt::Key_Left);
        QTest::keyClicks(&text, " ");
        require(text.text() == "1234 5" && requests.isEmpty() && player.paused() && window.geometry() == normal,
            "text editing owns arrows, numbers and Space");
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(&text, &preedit);
        QTest::keyClick(&text, Qt::Key_Right, Qt::ControlModifier);
        require(requests.isEmpty(), "IME preedit remains in the focused editor");
        QInputMethodEvent commit; commit.setCommitString(QStringLiteral("中")); QApplication::sendEvent(&text, &commit);
        text.hide();

        auto *volume = window.findChild<QSlider *>("volumeSlider");
        player.setAudioPreferences(25, true);
        require(until([&] { return volume->value() == 25; }), "volume control is ready");
        volume->setFocus();
        QTest::keyClick(volume, Qt::Key_Right);
        require(until([&] { return volume->value() == 26 && player.volumeTarget() == 26; }) && requests.isEmpty(),
            "volume slider retains its own arrow navigation");
        player.setMuted(true);

        window.findChild<PlaylistHost *>()->setVisible(true, false);
        auto *panel = window.findChild<PlaylistPanel *>();
        auto *list = panel->view();
        list->setFocus();
        list->setCurrentIndex(list->model()->index(0, 0));
        QTest::keyClick(list, Qt::Key_Right);
        QTest::keyClick(list, Qt::Key_Down);
        require(list->currentIndex().row() == 1 && requests.isEmpty() && player.paused(), "playlist retains its own navigation");
        window.findChild<PlaylistHost *>()->setVisible(false, false);

        QMenu menu(&window);
        menu.addAction(QStringLiteral("项目一")); auto *second = menu.addAction(QStringLiteral("项目二"));
        menu.popup(window.mapToGlobal(QPoint(100, 50)));
        require(until([&] { return QApplication::activePopupWidget() == &menu; }), "menu owns popup input");
        menu.setActiveAction(menu.actions().first());
        QTest::keyClick(&menu, Qt::Key_Down);
        QTest::keyClick(&menu, Qt::Key_Right);
        require(menu.activeAction() == second && requests.isEmpty(), "menu navigation does not seek");
        menu.close();
    }

    void settingsDialog()
    {
        bool fields = false, recorder = false, applied = false, draft = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            auto *small = dialog->findChild<QSpinBox *>("seekSmallSpin");
            auto *medium = dialog->findChild<QSpinBox *>("seekMediumSpin");
            auto *large = dialog->findChild<QSpinBox *>("seekLargeSpin");
            auto *tabs = dialog->findChild<QTabWidget *>("settingsTabs");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            fields = small->value() == 5 && medium->value() == 30 && large->value() == 60
                && small->minimum() == 1 && small->maximum() == 3600;
            small->setFocus();
            QTest::keyClick(small, Qt::Key_Right);
            QTest::keyClick(small, Qt::Key_Up);
            fields &= small->value() == 6 && requests.isEmpty();
            small->setValue(7); medium->setValue(17); large->setValue(47);
            tabs->setCurrentIndex(1);
            auto *key = dialog->findChild<QKeySequenceEdit *>("shortcutFirst");
            key->setFocus(); QTest::keyClick(key, Qt::Key_Right, Qt::ShiftModifier);
            recorder = key->keySequence() == QKeySequence(Qt::SHIFT | Qt::Key_Right) && requests.isEmpty() && player.paused();
            tabs->setCurrentIndex(0);
            buttons->button(QDialogButtonBox::Apply)->click();
            const auto saved = settings.load();
            applied = saved.seekSmallSeconds == 7 && saved.seekMediumSeconds == 17 && saved.seekLargeSeconds == 47;
            buttons->button(QDialogButtonBox::RestoreDefaults)->click();
            draft = small->value() == 5 && medium->value() == 30 && large->value() == 60
                && settings.load().seekSmallSeconds == 7;
            dialog->grab().save(output.filePath("seek-settings.png"));
            buttons->button(QDialogButtonBox::Cancel)->click();
        });
        requests.clear();
        session.showSettings();
        require(fields && recorder && requests.isEmpty(), "numeric and shortcut editors take their own keys while settings are open");
        require(applied && draft && settings.load().seekSmallSeconds == 7, "apply persists three steps and cancel discards the later reset");
    }

    void labels()
    {
        const auto small = QStringLiteral("前进 7 秒"), middle = QStringLiteral("后退 17 秒"), large = QStringLiteral("前进 47 秒");
        const auto menu = window.findChild<QMenu *>("playMenu");
        require(menu->actions().contains(actions->get(PlayerActions::ForwardSmall))
            && actions->get(PlayerActions::ForwardSmall)->text() == small
            && actions->get(PlayerActions::BackMedium)->toolTip().contains(middle)
            && actions->get(PlayerActions::ForwardLarge)->text() == large, "play menu and action tips reflect saved steps");
        require(forward->toolTip().contains(small) && forward->accessibleName() == small
            && forward->accessibleDescription().contains(small) && slider->accessibleDescription().contains(large),
            "button and slider accessibility descriptions report the effective steps");
        bool help = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("shortcutsDialog");
            auto *table = dialog->findChild<QTableWidget *>("shortcutsTable");
            help = table->item(PlayerActions::ForwardSmall, 0)->text() == small
                && table->item(PlayerActions::BackMedium, 0)->text() == middle
                && table->item(PlayerActions::ForwardLarge, 0)->text() == large
                && table->item(PlayerActions::ForwardSmall, 2)->text().contains(QStringLiteral("进度条"));
            dialog->grab().save(output.filePath("seek-help.png"));
            dialog->reject();
        });
        activate(); video->setFocus(); QTest::keyClick(video, Qt::Key_F1);
        require(help, "F1 reads current seek action names and scope");
    }

    void write()
    {
        require(disabled(), "idle media disables seeking");
        const auto ids = openQueue();
        defaults();
        coalescing();
        focusSafety();
        const auto generation = player.generation();
        settingsDialog();
        require(player.generation() == generation, "apply does not reload media");
        activate();
        seek(70);
        keySeek(video, Qt::Key_Left, Qt::NoModifier, 63);
        buttonSeek(forward, 7);
        keySeek(slider, Qt::Key_Left, Qt::ShiftModifier, 53);
        keySeek(slider, Qt::Key_Right, Qt::ControlModifier, 100);
        labels();

        QTest::mouseClick(forward, Qt::RightButton); waitItem(ids.back());
        QTest::mouseClick(back, Qt::RightButton); waitItem(ids.front());
        require(!QApplication::activePopupWidget(), "right mouse buttons still switch playlist items");
        player.openFile(media.filePath("grid.png"));
        require(until([&] { return player.state() == Player::State::Loaded && video->canCapture() && player.seekable(); })
            && player.duration() == 0 && disabled(), "loaded image with zero duration disables seeking");
        requests.clear();
        QTest::keyClick(video, Qt::Key_Right);
        QTest::mouseClick(forward, Qt::LeftButton);
        require(requests.isEmpty(), "zero-duration media never submits a seek");
        player.openFile(media.filePath("broken.mkv"));
        require(player.state() == Player::State::Loading && disabled(), "loading disables seeking immediately");
        require(until([&] { return player.state() == Player::State::Error; }) && disabled(), "failed media also disables seeking");
        window.close(); player.shutdown();
    }

    void read()
    {
        require(settings.load().seekSmallSeconds == 7 && settings.load().seekMediumSeconds == 17 && settings.load().seekLargeSeconds == 47,
            "second process reads the last applied steps");
        openQueue();
        seek(70);
        keySeek(slider, Qt::Key_Right, Qt::NoModifier, 77);
        keySeek(video, Qt::Key_Left, Qt::ShiftModifier, 60);
        keySeek(slider, Qt::Key_Right, Qt::ControlModifier, 107);
        buttonSeek(back, -7);
        labels();

        auto p = session.preferences();
        const QStringList customKeys{"Ctrl+K", "3"};
        p.shortcuts["seek.forwardSmall"] = customKeys;
        p.shortcuts["seek.backSmall"] = QStringList{};
        require(session.applyPreferences(p), "customize one seek binding and clear another");
        require(slider->accessibleDescription().contains("Ctrl+K"), "timeline accessibility follows custom bindings");
        seek(20);
        keySeek(video, Qt::Key_K, Qt::ControlModifier, 27);
        keySeek(slider, Qt::Key_K, Qt::ControlModifier, 34);
        keySeek(slider, Qt::Key_3, Qt::NoModifier, 41);
        requests.clear();
        QTest::keyClick(slider, Qt::Key_Left); QTest::keyClick(slider, Qt::Key_Right);
        require(requests.isEmpty() && near(player.position(), 41), "cleared and replaced bindings do not leave native arrow seeks behind");
        QLineEdit editor(&window);
        editor.move(24, 80); editor.show(); editor.setFocus();
        QTest::keyClicks(&editor, "12345");
        require(editor.text() == "12345" && requests.isEmpty(), "even a bound number stays available for text input");
        editor.hide();
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            buttons->button(QDialogButtonBox::RestoreDefaults)->click();
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        session.showSettings();
        const auto reset = settings.load();
        require(reset.seekSmallSeconds == 5 && reset.seekMediumSeconds == 30 && reset.seekLargeSeconds == 60
            && reset.shortcuts.value("seek.forwardSmall").toStringList() == customKeys,
            "applying page defaults restores all three steps and preserves shortcuts");
        activate();
        keySeek(slider, Qt::Key_K, Qt::ControlModifier, 46);
        window.close(); player.shutdown();
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    std::setlocale(LC_NUMERIC, "C");
    const auto args = app.arguments();
    if (args.size() != 4 || (args[1] != "write" && args[1] != "read")) return 2;
    try {
        Probe probe(args[3], args[2]);
        if (args[1] == "write") probe.write();
        else probe.read();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s\n", error.what());
        return 1;
    }
}
