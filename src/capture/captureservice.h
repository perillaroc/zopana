#pragma once
#include <QObject>
#include <QImage>
class Player;
class VideoWidget;
class CaptureService final : public QObject
{
    Q_OBJECT
public:
    CaptureService(Player &player, VideoWidget &video, QObject *parent = nullptr);
    void source();
    void viewport();
    void setDirectory(const QString &path) { directory_ = path; }
    QString directory() const { return directory_; }
    QString lastSaved() const { return lastSaved_; }
signals:
    void saved(const QString &path, bool workerThread);
    void failed(const QString &message);
private:
    void write(QImage image, quint64 generation, quint64 epoch, const QString &media, double position, const QString &kind);
    Player &player_;
    VideoWidget &video_;
    QString directory_, lastSaved_;
};
