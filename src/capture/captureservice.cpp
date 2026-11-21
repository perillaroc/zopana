#include "captureservice.h"
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageWriter>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTransform>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct SaveResult { QSharedPointer<QTemporaryFile> image; QString name, error; bool worker = false; };
}
CaptureService::CaptureService(Player &player, VideoWidget &video, QObject *parent) : QObject(parent), player_(player), video_(video)
{
    setObjectName("captureService");
    directory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath("zopana");
    connect(&player_, &Player::sourceImageReady, this, [this](const QImage &image, quint64 generation, quint64 epoch,
              quint64 transformVersion, int rotation, const QString &path, double position) {
        write(image, generation, epoch, transformVersion, path, position, "source", rotation);
    });
    connect(&player_, &Player::fileOpening, this, &CaptureService::clearReference);
    connect(&player_, &Player::stopping, this, &CaptureService::clearReference);
    connect(&player_, &Player::mediaChanged, this, &CaptureService::updateReferenceFraming);
    connect(&player_, &Player::seekRequested, this, &CaptureService::showLiveViewport);
    connect(&video_, &VideoWidget::viewChanged, this, &CaptureService::updateReferenceFraming);
}
void CaptureService::source()
{
    if (canCaptureViewport()) player_.captureSource();
}

bool CaptureService::canCaptureViewport() const
{
    return player_.videoTransformReady() && video_.canCaptureTransform(player_.videoTransformVersion());
}

CaptureService::ViewportFrame CaptureService::liveViewport()
{
    if (!canCaptureViewport()) return {};
    const auto playback = player_.playbackIdentity();
    const auto version = player_.videoTransformVersion();
    const double position = player_.position();
    auto image = video_.grabFramebuffer();
    if (player_.playbackIdentity() != playback || player_.videoTransformVersion() != version
        || !player_.videoTransformReady()) return {};
    if (image.isNull() || !video_.canCaptureTransform(version)) {
        emit failed(QStringLiteral("无法读取当前取景画面"));
        return {};
    }
    return {std::move(image), position};
}

CaptureService::ViewportFrame CaptureService::displayedViewport()
{
    if (const auto *reference = shownReference()) return *reference;
    return liveViewport();
}

QImage CaptureService::viewportImage()
{
    return displayedViewport().image;
}

void CaptureService::viewport()
{
    auto frame = displayedViewport();
    if (frame.image.isNull()) return;
    write(std::move(frame.image), player_.generation(), player_.renderEpoch(), player_.videoTransformVersion(),
          player_.path(), frame.position, "viewport");
}

bool CaptureService::canPinReference() const
{
    return canCaptureViewport() && !player_.seekInProgress() && !video_.fullViewPreviewActive();
}

const CaptureService::ViewportFrame *CaptureService::shownReference() const
{
    return showingReference_ ? &reference_->frame : nullptr;
}

bool CaptureService::pinReference()
{
    if (!canPinReference()) return false;
    const auto framing = video_.framing();
    auto frame = liveViewport();
    if (frame.image.isNull() || video_.framing() != framing) return false;
    reference_ = ReferenceFrame{std::move(frame), player_.playbackIdentity(), framing};
    emit referenceChanged();
    return true;
}

void CaptureService::toggleReference()
{
    if (!reference_ || video_.fullViewPreviewActive()) return;
    showingReference_ = !showingReference_;
    emit referenceChanged();
}

void CaptureService::showLiveViewport()
{
    if (!showingReference_) return;
    showingReference_ = false;
    emit referenceChanged();
}

void CaptureService::clearReference()
{
    if (!reference_) return;
    showingReference_ = false;
    reference_.reset();
    emit referenceChanged();
}

void CaptureService::updateReferenceFraming()
{
    if (!reference_) return;
    if (player_.state() != Player::State::Loaded || !video_.rendererReady()
        || player_.playbackIdentity() != reference_->playback) {
        clearReference();
    } else if (video_.fullViewPreviewActive()) {
        showLiveViewport();
    } else if (video_.framing() != reference_->framing) {
        clearReference();
        emit referenceInvalidated();
    }
}
void CaptureService::write(QImage image, quint64 generation, quint64 epoch, quint64 transformVersion,
                           const QString &media, double position, const QString &kind, int rotation)
{
    if (image.isNull()) { emit failed(QStringLiteral("当前没有可保存的画面")); return; }
    const QString directory = directory_;
    QString stem = QFileInfo(media).completeBaseName().left(80);
    stem.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")), "_");
    const auto name = QStringLiteral("%1-%2s-%3-").arg(stem).arg(position,0,'f',3).arg(kind);
    auto *guiThread = QThread::currentThread();
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this,watcher,generation,epoch,transformVersion] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != player_.generation() || epoch != player_.renderEpoch() || transformVersion != player_.videoTransformVersion()) return;
        if (!result.error.isEmpty()) { emit failed(result.error); return; }
        const QFileInfo temp(result.image->fileName());
        const auto path = temp.dir().filePath(result.name + temp.suffix() + ".png");
        if (!result.image->rename(path)) {
            emit failed(QStringLiteral("截图发布失败：%1").arg(result.image->errorString()));
            return;
        }
        result.image->setAutoRemove(false);
        lastSaved_ = path; emit saved(path,result.worker);
    });
    watcher->setFuture(QtConcurrent::run([image = std::move(image),directory,name,guiThread,rotation]() mutable {
        SaveResult result; result.worker = QThread::currentThread() != guiThread;
        result.name = name;
        if (!QDir().mkpath(directory)) { result.error = QStringLiteral("无法创建截图目录：%1").arg(directory); return result; }
        auto file = QSharedPointer<QTemporaryFile>::create(QDir(directory).filePath(".capture.XXXXXXXXXXXX"));
        if (!file->open()) { result.error = QStringLiteral("无法写入截图：%1").arg(file->errorString()); return result; }
        if (rotation) image = image.transformed(QTransform().rotate(rotation));
        QImageWriter writer(file.data(),"PNG");
        if (!writer.write(image) || !file->flush()) { result.error = QStringLiteral("截图保存失败：%1").arg(writer.errorString()); return result; }
        file->close(); result.image = std::move(file); return result;
    }));
}
