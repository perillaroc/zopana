#pragma once

#include "viewgeometry.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <memory>
#include <optional>

class QOpenGLFramebufferObject;
struct mpv_handle;
struct mpv_render_context;

class VideoWidget final : public QOpenGLWidget
{
    Q_OBJECT

public:
    enum class InputMode { Feature, Standard };

    explicit VideoWidget(mpv_handle *handle, QWidget *parent = nullptr);
    ~VideoWidget() override;

    bool rendererReady() const { return renderer_ != nullptr; }
    void setInputMode(InputMode mode);
    void setVideoSize(QSize displaySize, int rotation = 0);
    void setVideoTransform(std::optional<quint64> version);
    void restoreView(double scale, QPointF focus);
    void resetView();
    void zoomSteps(double steps);
    double zoomScale() const { return view_.scale(); }
    QPointF viewFocus() const;
    bool cancelInput();
    bool gestureActive() const { return leftPressActive_; }
    bool canCapture() const { return renderer_ && hasFrame_ && view_.valid(); }
    bool canCaptureTransform(quint64 version) const { return canCapture() && paintedTransform_ == version; }

signals:
    void rendererReadyChanged(bool ready);
    void volumeStepsRequested(double steps);
    void playbackToggleRequested();
    void fullscreenRequested();
    void zoomChanged(double scale);
    void viewChanged();

protected:
    void initializeGL() override;
    void paintGL() override;
    void customEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void releaseRenderer();
    void cancelGestures();
    void prepareVideoBuffer();
    bool renderFrame(int fbo, QSize pixels);
    void skipFrame();
    void advanceZoom();
    void zoomAt(QPointF pointer, double steps);
    bool withinClickDistance(QPointF position) const;
    void dragTo(QPointF position);

    mpv_handle *player_;
    mpv_render_context *renderer_ = nullptr;
    QOpenGLContext *ownerContext_ = nullptr;
    QMetaObject::Connection destructionConnection_;
    bool hasFrame_ = false;
    std::unique_ptr<QOpenGLFramebufferObject> videoBuffer_;
    QSize bufferForSize_;
    bool bufferHasFrame_ = false;
    std::optional<quint64> videoTransform_, paintedTransform_;

    QSize displaySize_;
    int rotation_ = 0;
    ViewGeometry view_;
    ViewGeometry zoomStart_;
    QTimer zoomTimer_;
    QTimer clickTimer_;
    QElapsedTimer sinceZoomFrame_;
    QDeadlineTimer zoomDeadline_;
    QPointF zoomPointer_;
    double zoomTarget_ = 1;
    InputMode inputMode_ = InputMode::Feature;
    bool leftPressActive_ = false;
    bool suppressRelease_ = false;
    QPointF pressPosition_;
    QPointF dragOrigin_;
};
