#include "settingsstore.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <cmath>

SettingsStore::SettingsStore(QString directory) : directory_(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory) {}
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
    const auto flag=[&](const char *key,bool fallback) { const auto v=s.value(key,fallback); const auto text=v.toString().toLower(); return text=="true" || text=="1" ? true : text=="false" || text=="0" ? false : fallback; };
    const auto path=s.value("capture/directory").toString(); if(QDir::isAbsolutePath(path)) p.captureDirectory=path;
    p.audioLanguages=s.value("tracks/audioLanguages").toString(); p.subtitleLanguages=s.value("tracks/subtitleLanguages").toString();
    p.recordHistory=flag("history/enabled",p.recordHistory); p.resumePolicy=int(number("history/resumePolicy",0,0,2));
    p.subtitleScale=number("tracks/subtitleScale",1,.5,2); p.volume=number("audio/volume",100,0,100); p.muted=flag("audio/muted",false);
    p.standardMode=flag("input/standardMode",false); p.maximized=flag("window/maximized",false);
    p.mainGeometry=s.value("window/normalGeometry").toRect(); p.playlistGeometry=s.value("playlist/normalGeometry").toRect();
    p.detached=flag("playlist/detached",false); p.playlistVisible=flag("playlist/visible",false); p.dockWidth=int(number("playlist/dockWidth",320,260,2000));
    s.beginGroup("shortcuts"); for(const auto &key:s.childKeys()) p.shortcuts[key]=s.value(key).toStringList(); s.endGroup();
    error_=s.status()==QSettings::NoError ? QString() : QStringLiteral("部分设置无法读取，已采用有效值或默认值。"); return p;
}
bool SettingsStore::save(const Preferences &p)
{
    if(!QDir().mkpath(directory_)) { error_=QStringLiteral("无法创建设置目录：%1").arg(directory_); return false; }
    // Serialize readable keys into a fresh INI before replacing the destination.
    // QSettings retains FormatError on a malformed source even after sync().
    QTemporaryDir temporary(directory_+"/.settings-XXXXXX");
    if(!temporary.isValid()) { error_=QStringLiteral("无法创建临时设置文件：%1").arg(directory_); return false; }
    const auto temporaryPath=temporary.path()+"/settings.ini";
    QSettings original(fileName(),QSettings::IniFormat); original.sync();
    QSettings s(temporaryPath,QSettings::IniFormat); s.setAtomicSyncRequired(true);
    for(const auto &key:original.allKeys()) s.setValue(key,original.value(key));
    s.setValue("schemaVersion",qMax(1,s.value("schemaVersion",1).toInt()));
    s.setValue("capture/directory",p.captureDirectory); s.setValue("tracks/audioLanguages",p.audioLanguages); s.setValue("tracks/subtitleLanguages",p.subtitleLanguages);
    s.setValue("history/enabled",p.recordHistory); s.setValue("history/resumePolicy",p.resumePolicy); s.setValue("tracks/subtitleScale",p.subtitleScale);
    s.setValue("audio/volume",p.volume); s.setValue("audio/muted",p.muted); s.setValue("input/standardMode",p.standardMode);
    s.setValue("window/normalGeometry",p.mainGeometry); s.setValue("window/maximized",p.maximized);
    s.setValue("playlist/normalGeometry",p.playlistGeometry); s.setValue("playlist/detached",p.detached); s.setValue("playlist/visible",p.playlistVisible); s.setValue("playlist/dockWidth",p.dockWidth);
    s.beginGroup("shortcuts");
    s.remove(""); // The loaded map also retains unknown future command IDs.
    for(auto it=p.shortcuts.begin();it!=p.shortcuts.end();++it) s.setValue(it.key(),it.value()); s.endGroup(); s.sync();
    QFile encoded(temporaryPath); QSaveFile destination(fileName());
    const bool success=s.status()==QSettings::NoError && encoded.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
    const auto bytes=success?encoded.readAll():QByteArray();
    const bool saved=success && destination.write(bytes)==bytes.size() && destination.commit();
    error_=saved ? QString() : QStringLiteral("设置保存失败：%1").arg(fileName()); return saved;
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
