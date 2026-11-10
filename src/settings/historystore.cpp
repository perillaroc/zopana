#include "historystore.h"
#include "mediastate.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

bool HistoryEntry::matchesFile() const { const QFileInfo f(path); return f.isFile() && f.size()==size && f.lastModified().toMSecsSinceEpoch()==modified; }
HistoryStore::HistoryStore(QString directory,QObject *parent) : QObject(parent),directory_(directory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation):directory)
{
    QFile file(fileName());
    if(file.exists()) {
        QJsonParseError error; const bool readable=file.open(QIODevice::ReadOnly); const auto doc=QJsonDocument::fromJson(readable?file.readAll():QByteArray(),&error);
        if(!readable || error.error!=QJsonParseError::NoError || !doc.isObject() || !doc.object().value("entries").isArray()) loadError_=QStringLiteral("历史文件无法读取，已忽略损坏内容。");
        else {
            extra_=doc.object(); QSet<QString> seen;
            for(const auto &value:extra_.value("entries").toArray()) {
                const auto o=value.toObject(); HistoryEntry e; e.extra=o; e.path=o.value("path").toString(); e.size=qint64(o.value("size").toDouble(-1)); e.modified=qint64(o.value("modified").toDouble()); e.played=qint64(o.value("played").toDouble());
                e.position=o.value("positionSeconds").toDouble(); e.duration=o.value("durationSeconds").toDouble();
                const auto key=mediaPathKey(e.path);
                if(!QDir::isAbsolutePath(e.path) || e.size<0 || !std::isfinite(e.position) || !std::isfinite(e.duration) || e.position<0 || e.duration<0 || seen.contains(key)) continue;
                seen.insert(key); entries_.append(e);
            }
            std::stable_sort(entries_.begin(),entries_.end(),[](const auto &a,const auto &b) { return a.played>b.played; });
            if(entries_.size()>50) entries_=entries_.mid(0,50);
        }
    }
    timer_.setSingleShot(true); timer_.setInterval(80); connect(&timer_,&QTimer::timeout,this,&HistoryStore::flush);
    connect(&writer_,&QFutureWatcher<QString>::finished,this,&HistoryStore::finished);
}
HistoryStore::~HistoryStore() { waitForSaved(); }
std::optional<HistoryEntry> HistoryStore::find(const QString &path) const { const auto key=mediaPathKey(path); for(const auto &entry:entries_) if(mediaPathKey(entry.path)==key) return entry; return {}; }
void HistoryStore::remember(const QString &path,double position,double duration,bool ended)
{
    const QFileInfo f(path); if(!f.isFile() || !std::isfinite(position) || !std::isfinite(duration) || duration<=0) return;
    HistoryEntry e=find(path).value_or(HistoryEntry{}); const auto key=mediaPathKey(path);
    entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const auto &entry) { return mediaPathKey(entry.path)==key; }),entries_.end());
    e.path=f.canonicalFilePath(); if(e.path.isEmpty()) e.path=f.absoluteFilePath(); e.size=f.size(); e.modified=f.lastModified().toMSecsSinceEpoch(); e.played=QDateTime::currentMSecsSinceEpoch();
    e.position=ended?0:std::clamp(position,0.0,duration); e.duration=duration; entries_.prepend(e); if(entries_.size()>50) entries_.removeLast(); dirty_=true; timer_.start(); emit changed();
}
void HistoryStore::clear() { entries_.clear(); dirty_=true; timer_.stop(); emit changed(); flush(); }
void HistoryStore::flush()
{
    timer_.stop(); if(!dirty_ || writing_) return; dirty_=false; writing_=true;
    QJsonObject root=extra_; root["schemaVersion"]=qMax(1,root.value("schemaVersion").toInt(1)); QJsonArray rows;
    for(const auto &e:entries_) { auto o=e.extra; o["schemaVersion"]=qMax(1,o.value("schemaVersion").toInt(1)); o["path"]=e.path; o["size"]=double(e.size); o["modified"]=double(e.modified); o["played"]=double(e.played); o["positionSeconds"]=e.position; o["durationSeconds"]=e.duration; rows.append(o); }
    root["entries"]=rows; const auto bytes=QJsonDocument(root).toJson(); const auto directory=directory_,path=fileName();
    writer_.setFuture(QtConcurrent::run([directory,path,bytes]() -> QString { if(!QDir().mkpath(directory)) return QStringLiteral("无法创建历史目录：%1").arg(directory); QSaveFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) return QStringLiteral("历史保存失败：%1").arg(path); return {}; }));
}
void HistoryStore::finished()
{
    if(!writing_) return; writing_=false; const auto message=writer_.result();
    if(!message.isEmpty()) { dirty_=true; if(!errorReported_) { errorReported_=true; emit error(message); } }
    else { errorReported_=false; emit saved(); if(dirty_) flush(); }
}
void HistoryStore::waitForSaved()
{
    timer_.stop(); if(writing_) { writer_.waitForFinished(); finished(); }
    if(dirty_) { flush(); if(writing_) { writer_.waitForFinished(); finished(); } }
}
