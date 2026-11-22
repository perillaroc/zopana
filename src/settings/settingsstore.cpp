#include "settingsstore.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr qlonglong schemaVersion = 2;
constexpr const char *legacyViewKeys[] = {"video/viewScale", "video/focusU", "video/focusV"};
constexpr std::pair<RepeatMode, const char *> repeatModes[] = {
    {RepeatMode::None, "none"}, {RepeatMode::One, "one"}, {RepeatMode::All, "all"}
};
constexpr std::pair<SortField, const char *> sortFields[] = {
    {SortField::FileName, "fileName"}, {SortField::Title, "title"}, {SortField::Extension, "extension"},
    {SortField::Folder, "folder"}, {SortField::Path, "path"}, {SortField::FileSize, "fileSize"},
    {SortField::ModifiedTime, "modifiedTime"}, {SortField::CreatedTime, "createdTime"},
    {SortField::Duration, "duration"}, {SortField::Resolution, "resolution"}, {SortField::AddedOrder, "addedOrder"}
};
constexpr std::pair<SortDirection, const char *> sortDirections[] = {
    {SortDirection::Ascending, "ascending"}, {SortDirection::Descending, "descending"}
};
constexpr std::pair<WindowSizePolicy, const char *> sizePolicies[] = {
    {WindowSizePolicy::LastGeometry, "lastGeometry"}, {WindowSizePolicy::VideoScale, "videoScale"},
    {WindowSizePolicy::FitWorkArea, "fitWorkArea"}
};

template<class Enum, size_t N>
Enum readEnum(const QSettings &settings, const char *key, Enum fallback, const std::pair<Enum, const char *> (&values)[N])
{
    const auto text = settings.value(key).toString();
    for (const auto &[value, name] : values) if (text == name) return value;
    return fallback;
}

template<class Enum, size_t N>
void writeEnum(QSettings &settings, const char *key, Enum value, const std::pair<Enum, const char *> (&values)[N])
{
    for (const auto &[candidate, name] : values) {
        if (candidate == value) {
            settings.setValue(key, QString::fromLatin1(name));
            return;
        }
    }
    settings.setValue(key, QString::fromLatin1(values[0].second));
}
}

SettingsStore::SettingsStore(QString directory, std::function<void()> beforeWrite)
    : directory_(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory),
      beforeWrite_(std::move(beforeWrite))
{
}
void SettingsStore::setApplicationIdentity()
{
    QCoreApplication::setOrganizationName("zopana"); QCoreApplication::setApplicationName("zopana");
}
Preferences SettingsStore::defaults()
{
    Preferences p; p.captureDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("zopana"); return p;
}
Preferences SettingsStore::load()
{
    Preferences p=defaults(); QSettings s(fileName(),QSettings::IniFormat); s.sync();
    const auto number=[&](const char *key,double fallback,double low,double high) { bool ok=false; const double n=s.value(key,fallback).toDouble(&ok); return ok && std::isfinite(n) && n>=low && n<=high ? n : fallback; };
    const auto integer = [&](const char *key, int fallback, int low, int high) {
        bool ok = false;
        const int value = s.value(key).toString().toInt(&ok);
        return ok && value >= low && value <= high ? value : fallback;
    };
    const auto flag=[&](const char *key,bool fallback) { const auto v=s.value(key,fallback); const auto text=v.toString().trimmed().toLower(); return text=="true" || text=="1" ? true : text=="false" || text=="0" ? false : fallback; };
    const auto path=s.value("capture/directory").toString(); if(QDir::isAbsolutePath(path)) p.captureDirectory=path;
    p.audioLanguages=s.value("tracks/audioLanguages").toString(); p.subtitleLanguages=s.value("tracks/subtitleLanguages").toString();
    p.recordHistory=flag("history/enabled",p.recordHistory); p.resumePolicy=integer("history/resumePolicy",p.resumePolicy,0,2);
    p.subtitleScale=number("tracks/subtitleScale",1,.5,2); p.volume=number("audio/volume",100,0,100); p.muted=flag("audio/muted",false);
    p.speed = number("playback/speed", p.speed, .25, 3);
    p.pitchCorrection = flag("playback/pitchCorrection", p.pitchCorrection);
    p.subtitleVisible = flag("tracks/subtitleVisible", p.subtitleVisible);
    p.audioDelay = number("tracks/audioDelay", p.audioDelay, -3600, 3600);
    p.subtitleDelay = number("tracks/subtitleDelay", p.subtitleDelay, -3600, 3600);
    p.seekSmallSeconds = integer("playback/seekSmallSeconds", p.seekSmallSeconds, Preferences::minSeekStepSeconds, Preferences::maxSeekStepSeconds);
    p.seekMediumSeconds = integer("playback/seekMediumSeconds", p.seekMediumSeconds, Preferences::minSeekStepSeconds, Preferences::maxSeekStepSeconds);
    p.seekLargeSeconds = integer("playback/seekLargeSeconds", p.seekLargeSeconds, Preferences::minSeekStepSeconds, Preferences::maxSeekStepSeconds);
    p.skipEnabled = flag("skip/enabled", p.skipEnabled);
    p.introEnabled = flag("skip/introEnabled", p.introEnabled);
    p.outroEnabled = flag("skip/outroEnabled", p.outroEnabled);
    p.introSeconds = integer("skip/introSeconds", p.introSeconds, 0, Preferences::maxSkipSeconds);
    p.outroSeconds = integer("skip/outroSeconds", p.outroSeconds, 0, Preferences::maxSkipSeconds);
    p.repeatMode = readEnum(s, "playback/repeatMode", p.repeatMode, repeatModes);
    p.shuffleEnabled = flag("playback/shuffleEnabled", p.shuffleEnabled);
    p.sortField = readEnum(s, "playlist/sortField", p.sortField, sortFields);
    p.sortDirection = readEnum(s, "playlist/sortDirection", p.sortDirection, sortDirections);
    const int rotation = integer("video/userRotationDeg", p.userRotationDeg, 0, 270);
    if (rotation % 90 == 0) p.userRotationDeg = rotation;
    p.resetViewOnOpen = flag("video/resetViewOnOpen", p.resetViewOnOpen);
    p.sizePolicy = readEnum(s, "window/sizePolicy", p.sizePolicy, sizePolicies);
    const double scale = number("window/lastVideoScale", p.lastVideoScale, .5, 2);
    if (scale == .5 || scale == 1 || scale == 1.5 || scale == 2) p.lastVideoScale = scale;
    p.alwaysOnTop = flag("window/alwaysOnTop", p.alwaysOnTop);
    p.standardMode=flag("input/standardMode",false); p.maximized=flag("window/maximized",false);
    p.mainGeometry=s.value("window/normalGeometry").toRect(); p.playlistGeometry=s.value("playlist/normalGeometry").toRect();
    p.detached=flag("playlist/detached",false); p.playlistVisible=flag("playlist/visible",false); p.dockWidth=integer("playlist/dockWidth",p.dockWidth,260,2000);
    s.beginGroup("shortcuts"); for(const auto &key:s.allKeys()) p.shortcuts[key]=s.value(key); s.endGroup();
    needsWrite_ = QFile::exists(fileName()) && (s.value("schemaVersion").toString().toLongLong() < schemaVersion
        || s.status() == QSettings::FormatError
        || std::ranges::any_of(legacyViewKeys, [&s](const char *key) { return s.contains(key); }));
    error_=s.status()==QSettings::NoError ? QString() : QStringLiteral("部分设置无法读取，已采用有效值或默认值。"); return p;
}
bool SettingsStore::save(const Preferences &p)
{
    return finishSave(write(p));
}
QFuture<QString> SettingsStore::saveInBackground(const Preferences &p) const
{
    return QtConcurrent::run([store = *this, p] { return store.write(p); });
}
bool SettingsStore::finishSave(QString error)
{
    error_ = std::move(error);
    if (!error_.isEmpty()) return false;
    needsWrite_ = false;
    return true;
}
QString SettingsStore::write(const Preferences &p) const
{
    if (!QDir().mkpath(directory_)) return QStringLiteral("无法创建设置目录：%1").arg(directory_);
    // Serialize readable keys into a fresh INI before replacing the destination.
    // QSettings retains FormatError on a malformed source even after sync().
    QTemporaryDir temporary(directory_+"/.settings-XXXXXX");
    if (!temporary.isValid()) return QStringLiteral("无法创建临时设置文件：%1").arg(directory_);
    const auto temporaryPath=temporary.path()+"/settings.ini";
    QSettings original(fileName(),QSettings::IniFormat); original.sync();
    if (original.status() == QSettings::AccessError)
        return QStringLiteral("无法读取设置文件：%1").arg(fileName());
    QSettings s(temporaryPath,QSettings::IniFormat); s.setAtomicSyncRequired(true);
    for(const auto &key:original.allKeys()) s.setValue(key,original.value(key));
    s.setValue("schemaVersion", qMax(schemaVersion, original.value("schemaVersion").toString().toLongLong()));
    s.setValue("capture/directory",p.captureDirectory); s.setValue("tracks/audioLanguages",p.audioLanguages); s.setValue("tracks/subtitleLanguages",p.subtitleLanguages);
    s.setValue("history/enabled",p.recordHistory); s.setValue("history/resumePolicy",p.resumePolicy); s.setValue("tracks/subtitleScale",p.subtitleScale);
    s.setValue("audio/volume",p.volume); s.setValue("audio/muted",p.muted); s.setValue("input/standardMode",p.standardMode);
    s.setValue("playback/speed", p.speed);
    s.setValue("playback/pitchCorrection", p.pitchCorrection);
    s.setValue("tracks/subtitleVisible", p.subtitleVisible);
    s.setValue("tracks/audioDelay", p.audioDelay);
    s.setValue("tracks/subtitleDelay", p.subtitleDelay);
    s.setValue("playback/seekSmallSeconds", p.seekSmallSeconds);
    s.setValue("playback/seekMediumSeconds", p.seekMediumSeconds);
    s.setValue("playback/seekLargeSeconds", p.seekLargeSeconds);
    s.setValue("skip/enabled", p.skipEnabled);
    s.setValue("skip/introEnabled", p.introEnabled);
    s.setValue("skip/outroEnabled", p.outroEnabled);
    s.setValue("skip/introSeconds", p.introSeconds);
    s.setValue("skip/outroSeconds", p.outroSeconds);
    writeEnum(s, "playback/repeatMode", p.repeatMode, repeatModes);
    s.setValue("playback/shuffleEnabled", p.shuffleEnabled);
    writeEnum(s, "playlist/sortField", p.sortField, sortFields);
    writeEnum(s, "playlist/sortDirection", p.sortDirection, sortDirections);
    for (const auto *key : legacyViewKeys) s.remove(key);
    s.setValue("video/userRotationDeg", p.userRotationDeg);
    s.setValue("video/resetViewOnOpen", p.resetViewOnOpen);
    writeEnum(s, "window/sizePolicy", p.sizePolicy, sizePolicies);
    s.setValue("window/lastVideoScale", p.lastVideoScale);
    s.setValue("window/alwaysOnTop", p.alwaysOnTop);
    s.setValue("window/normalGeometry",p.mainGeometry); s.setValue("window/maximized",p.maximized);
    s.setValue("playlist/normalGeometry",p.playlistGeometry); s.setValue("playlist/detached",p.detached); s.setValue("playlist/visible",p.playlistVisible); s.setValue("playlist/dockWidth",p.dockWidth);
    s.beginGroup("shortcuts");
    s.remove(""); // The loaded map also retains unknown future command IDs.
    for(auto it=p.shortcuts.begin();it!=p.shortcuts.end();++it) s.setValue(it.key(),it.value());
    s.endGroup();
    if (beforeWrite_) beforeWrite_();
    s.sync();
    QFile encoded(temporaryPath); QSaveFile destination(fileName());
    const bool success=s.status()==QSettings::NoError && encoded.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
    const auto bytes=success?encoded.readAll():QByteArray();
    const bool saved=success && encoded.error()==QFileDevice::NoError && destination.write(bytes)==bytes.size() && destination.commit();
    return saved ? QString() : QStringLiteral("设置保存失败：%1").arg(fileName());
}
QRect SettingsStore::visibleGeometry(QRect requested,const QList<QRect> &screens,QSize minimum)
{
    if(screens.isEmpty()) return requested;
    QRect area=screens.first(); int best=0;
    for(const auto &screen:screens) { const auto overlap=screen.intersected(requested); const int size=overlap.width()*overlap.height(); if(size>best) { best=size; area=screen; } }
    area.setTop(area.top()+qMin(32,area.height()/10));
    if(!requested.isValid()) requested=QRect(area.center()-QPoint(minimum.width()/2,minimum.height()/2),minimum);
    requested.setSize(requested.size().expandedTo(minimum).boundedTo(area.size().expandedTo(minimum)));
    requested.moveLeft(qBound(area.left(),requested.left(),qMax(area.left(),area.right()-requested.width()+1)));
    requested.moveTop(qBound(area.top(),requested.top(),qMax(area.top(),area.bottom()-requested.height()+1)));
    return requested;
}
