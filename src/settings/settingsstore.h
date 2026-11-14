#pragma once
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVariantMap>

enum class RepeatMode { None, One, All };
enum class SortField { FileName, Title, Extension, Folder, Path, FileSize, ModifiedTime, CreatedTime, Duration, Resolution, AddedOrder };
enum class SortDirection { Ascending, Descending };
enum class WindowSizePolicy { LastGeometry, VideoScale, FitWorkArea };

struct Preferences {
    static constexpr int minSeekStepSeconds = 1, maxSeekStepSeconds = 3600;

    QString captureDirectory, audioLanguages, subtitleLanguages;
    bool recordHistory = false;
    int resumePolicy = 0; // Ask, always resume, start over.
    double subtitleScale = 1, volume = 100;
    bool muted = false, standardMode = false;
    double speed = 1;
    bool pitchCorrection = true, subtitleVisible = true;
    double audioDelay = 0, subtitleDelay = 0;
    int seekSmallSeconds = 5, seekMediumSeconds = 30, seekLargeSeconds = 60;
    bool skipEnabled = false, introEnabled = false, outroEnabled = false;
    int introSeconds = 0, outroSeconds = 0;
    RepeatMode repeatMode = RepeatMode::None;
    bool shuffleEnabled = false;
    SortField sortField = SortField::FileName;
    SortDirection sortDirection = SortDirection::Ascending;
    double viewScale = 1;
    QPointF viewFocus{0.5, 0.5};
    int userRotationDeg = 0;
    bool resetViewOnOpen = false;
    WindowSizePolicy sizePolicy = WindowSizePolicy::LastGeometry;
    double lastVideoScale = 1;
    QRect mainGeometry, playlistGeometry;
    bool maximized = false, alwaysOnTop = false, detached = false, playlistVisible = false;
    int dockWidth = 320;
    QVariantMap shortcuts;

    bool operator==(const Preferences &) const = default;
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
    bool needsWrite() const { return needsWrite_; }
private:
    QString directory_, error_;
    bool needsWrite_ = false;
};
