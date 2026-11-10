#pragma once
#include <QObject>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QTimer>
#include <optional>

struct HistoryEntry {
    QString path; qint64 size=0, modified=0, played=0;
    double position=0, duration=0;
    QJsonObject extra;
    bool matchesFile() const;
    bool resumable() const { return matchesFile() && position>30 && duration-position>30; }
};
class HistoryStore final : public QObject
{
    Q_OBJECT
public:
    explicit HistoryStore(QString directory = {},QObject *parent=nullptr);
    ~HistoryStore() override;
    const QList<HistoryEntry> &entries() const { return entries_; }
    std::optional<HistoryEntry> find(const QString &path) const;
    void remember(const QString &path,double position,double duration,bool ended=false);
    void clear();
    void flush();
    void waitForSaved();
    QString fileName() const { return directory_+"/history.json"; }
    QString loadError() const { return loadError_; }
signals:
    void changed();
    void saved();
    void error(const QString &message);
private:
    void finished();
    QString directory_,loadError_;
    QList<HistoryEntry> entries_;
    QJsonObject extra_;
    QFutureWatcher<QString> writer_;
    QTimer timer_;
    bool dirty_=false,writing_=false,errorReported_=false;
};
