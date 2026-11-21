#pragma once
#include "player.h"
#include "videowidget.h"

#include <QObject>
#include <QImage>
#include <optional>

class CaptureService final : public QObject
{
    Q_OBJECT
public:
    struct ViewportFrame {
        QImage image;
        double position = 0;
    };

    CaptureService(Player &player, VideoWidget &video, QObject *parent = nullptr);
    void source();
    void viewport();
    bool canCaptureViewport() const;
    QImage viewportImage();
    bool canPinReference() const;
    bool hasReference() const { return reference_.has_value(); }
    const ViewportFrame *shownReference() const;
    bool pinReference();
    void toggleReference();
    void showLiveViewport();
    void clearReference();
    void setDirectory(const QString &path) { directory_ = path; }
    QString directory() const { return directory_; }
    QString lastSaved() const { return lastSaved_; }
signals:
    void saved(const QString &path, bool workerThread);
    void failed(const QString &message);
    void referenceChanged();
    void referenceInvalidated();

private:
    struct ReferenceFrame {
        ViewportFrame frame;
        Player::PlaybackIdentity playback;
        VideoWidget::Framing framing;
    };

    ViewportFrame liveViewport();
    ViewportFrame displayedViewport();
    void updateReferenceFraming();
    void write(QImage image, quint64 generation, quint64 epoch, quint64 transformVersion,
               const QString &media, double position, const QString &kind, int rotation = 0);
    Player &player_;
    VideoWidget &video_;
    QString directory_, lastSaved_;
    std::optional<ReferenceFrame> reference_;
    bool showingReference_ = false;
};
