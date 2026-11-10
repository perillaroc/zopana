#include "captureservice.h"
#include "player.h"
#include "videowidget.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageWriter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>

namespace { struct SaveResult { QString path, error; bool worker = false; }; }
CaptureService::CaptureService(Player &player, VideoWidget &video, QObject *parent) : QObject(parent), player_(player), video_(video)
{
    setObjectName("captureService");
    directory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("zopana");
    connect(&player_, &Player::sourceImageReady, this, [this](const QImage &image, quint64 generation, quint64 epoch, const QString &path, double position) {
        write(image,generation,epoch,path,position,"source");
    });
}
void CaptureService::source() { if (video_.canCapture()) player_.captureSource(); }
void CaptureService::viewport()
{
    if (!video_.canCapture()) return;
    write(video_.grabFramebuffer(),player_.generation(),player_.renderEpoch(),player_.path(),player_.positionTarget(),"viewport");
}
void CaptureService::write(QImage image, quint64 generation, quint64 epoch, const QString &media, double position, const QString &kind)
{
    if (image.isNull()) { emit failed(QStringLiteral("当前没有可保存的画面")); return; }
    const QString directory = directory_;
    QString stem = QFileInfo(media).completeBaseName().left(80);
    stem.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")), "_");
    const auto name = QStringLiteral("%1-%2s-%3-XXXXXX.png").arg(stem).arg(position,0,'f',3).arg(kind);
    auto *guiThread = QThread::currentThread();
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this,watcher,generation,epoch] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != player_.generation() || epoch != player_.renderEpoch()) return;
        if (!result.error.isEmpty()) { emit failed(result.error); return; }
        lastSaved_ = result.path; emit saved(result.path,result.worker);
    });
    watcher->setFuture(QtConcurrent::run([image = std::move(image),directory,name,guiThread] {
        SaveResult result; result.worker = QThread::currentThread() != guiThread;
        if (!QDir().mkpath(directory)) { result.error = QStringLiteral("无法创建截图目录：%1").arg(directory); return result; }
        QTemporaryFile file(QDir(directory).filePath(name));
        if (!file.open()) { result.error = QStringLiteral("无法写入截图：%1").arg(file.errorString()); return result; }
        QImageWriter writer(&file,"PNG");
        if (!writer.write(image) || !file.flush()) { result.error = QStringLiteral("截图保存失败：%1").arg(writer.errorString()); return result; }
        result.path = file.fileName(); file.setAutoRemove(false); file.close(); return result;
    }));
}
