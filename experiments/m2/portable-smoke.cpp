// Runs the production MainWindow/core against only the deployed runtime DLLs.
#include "mainwindow.h"
#include "player.h"
#include "videowidget.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <windows.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setApplicationName("zopana");
    if (app.arguments().size() != 3) return 2;
    Player player; if (!player.initialize()) return 3;
    MainWindow window(player);
    auto *video = window.findChild<VideoWidget *>("videoWidget");
    bool opened = false, loaded = false, paused = false, fullscreen = false;
    QObject::connect(&player, &Player::mediaChanged, &window, [&](auto state, const QString &, const QString &) {
        loaded = state == Player::State::Loaded;
        if (loaded) player.setPaused(true);
    });
    QObject::connect(&player, &Player::pausedChanged, &window, [&](bool value) { paused = value; });
    QTimer poll; poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &window, [&] {
        if (!opened && video->rendererReady()) { opened = true; player.openFile(app.arguments()[1]); }
        if (!loaded || !paused) return;
        if (!fullscreen) { window.findChild<QAction *>("window.fullscreen")->trigger(); fullscreen = true; return; }
        QJsonObject modules;
        for (const auto *name : {L"Qt6Core.dll", L"Qt6Gui.dll", L"Qt6Widgets.dll", L"Qt6OpenGL.dll", L"Qt6OpenGLWidgets.dll", L"Qt6Svg.dll", L"libmpv-2.dll"}) {
            wchar_t path[32768]{}; GetModuleFileNameW(GetModuleHandleW(name), path, 32768);
            modules[QString::fromWCharArray(name)] = QDir::fromNativeSeparators(QString::fromWCharArray(path));
        }
        const bool passed = window.isFullScreen() && video->isValid();
        QFile report(app.arguments()[2]);
        if (!report.open(QIODevice::WriteOnly)) { app.exit(4); return; }
        report.write(QJsonDocument(QJsonObject{{"passed", passed}, {"loaded", loaded}, {"paused", paused},
            {"fullscreen", window.isFullScreen()}, {"modules", modules}}).toJson()); report.close();
        window.close(); app.exit(passed ? 0 : 5);
    });
    QTimer::singleShot(10000, &app, [&] { app.exit(6); });
    window.show(); poll.start(); const int result = app.exec(); player.shutdown(); return result;
}
