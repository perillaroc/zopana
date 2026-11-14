#pragma once

#include "playlistmetadata.h"

#include <QProcess>
#include <QTimer>
#include <QVector>

class MetadataProbe final : public QObject
{
    Q_OBJECT
public:
    struct Result {
        std::optional<PlaylistFileInfo> file;
        std::optional<PlaylistMetadata> media;
        bool failedToStart = false;
    };
    struct Check { QString path; std::optional<PlaylistFileInfo> file; };

    explicit MetadataProbe(QObject *parent = nullptr);
    ~MetadataProbe() override;
    quint64 read(const QString &path, bool media, const std::optional<PlaylistFileInfo> &cached = {});
    quint64 checkVersions(const QVector<Check> &checks);
    void cancel();

    static constexpr auto workerArgument = "--playlist-metadata-worker";
    static int runWorker();

signals:
    void finished(quint64 request, const QString &path, const MetadataProbe::Result &result);
    void versionsChecked(quint64 request, const QVector<int> &changed, bool verified);

private:
    struct Request { quint64 id; QString path; QByteArray line; int checkCount = 0; };
    void start();
    void send();
    void receive();
    void killWorker();
    void complete(const Result &result);
    void completeChecks(const QByteArray &line);
    QProcess process_;
    QTimer timeout_;
    std::optional<Request> request_;
    quint64 nextRequest_ = 1;
    bool stopping_ = false;
};
