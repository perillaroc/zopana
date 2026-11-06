#include "mainwindow.h"
#include "player.h"

#include <QApplication>

#include <clocale>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Zopana"));
    std::setlocale(LC_NUMERIC, "C");
    std::fprintf(stderr, "Qt runtime: %s\n", qVersion());

    Player player;
    if (!player.initialize())
        return EXIT_FAILURE;

    MainWindow window(player);
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
