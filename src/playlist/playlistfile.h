#pragma once

#include <QStringList>

namespace PlaylistFile {
struct Contents {
    QStringList paths;
    qsizetype invalidPaths = 0;
    qsizetype networkPaths = 0;
    QString error;
};

Contents read(const QString &fileName);
QString write(const QString &fileName, const QStringList &paths);
}
