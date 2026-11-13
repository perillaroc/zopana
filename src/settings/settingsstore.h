#pragma once
#include <QRect>
#include <QString>
#include <QVariantMap>

struct Preferences {
    QString captureDirectory, audioLanguages, subtitleLanguages;
    bool recordHistory = false;
    int resumePolicy = 0; // Ask, always resume, start over.
    double subtitleScale = 1, volume = 100;
    bool muted = false, standardMode = false;
    QRect mainGeometry, playlistGeometry;
    bool maximized = false, detached = false, playlistVisible = false;
    int dockWidth = 320;
    QVariantMap shortcuts;
};

class SettingsStore
{
public:
    explicit SettingsStore(QString directory = {});
    static void setApplicationIdentity();
    static Preferences defaults();
    static QRect visibleGeometry(QRect requested, const QList<QRect> &screens, QSize minimum);
    Preferences load();
    bool save(const Preferences &preferences);
    QString fileName() const { return directory_ + "/settings.ini"; }
    QString error() const { return error_; }
private:
    QString directory_, error_;
};
