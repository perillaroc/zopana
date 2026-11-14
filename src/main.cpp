#include "mainwindow.h"
#include "player.h"
#include "settingsstore.h"
#include "historystore.h"
#include "sessioncontroller.h"
#include "metadataprobe.h"

#include <QApplication>
#include <QDir>

#include <clocale>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    std::setlocale(LC_NUMERIC, "C");
    if (argc == 2 && QByteArrayView(argv[1]) == MetadataProbe::workerArgument)
        return MetadataProbe::runWorker();

    QApplication app(argc, argv);
    SettingsStore::setApplicationIdentity();
    std::fprintf(stderr, "Qt runtime: %s\n", qVersion());

    // Release verification can run without touching the operating user's data.
    QDir profile;
    const auto arguments = app.arguments();
    const bool isolated = arguments.value(1) == "--validation-profile";
    if (isolated) {
        if (arguments.size() != 3 || !QDir::isAbsolutePath(arguments[2])) {
            std::fputs("--validation-profile requires an absolute directory\n", stderr);
            return EXIT_FAILURE;
        }
        profile.setPath(arguments[2]);
    }
    SettingsStore settings(isolated ? profile.filePath("config") : QString{});
    HistoryStore history(isolated ? profile.filePath("data") : QString{});
    Player player;
    if (!player.initialize())
        return EXIT_FAILURE;

    MainWindow window(player);
    SessionController session(player,window,settings,history);
    window.show();
    if (!window.videoContextValid()) {
        std::fputs("QOpenGLWidget failed to create its OpenGL context\n", stderr);
        player.shutdown();
        return EXIT_FAILURE;
    }

    const int result = app.exec();
    player.shutdown();
    return result;
}
