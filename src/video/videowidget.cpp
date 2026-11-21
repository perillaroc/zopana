#include "videowidget.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QResizeEvent>
#include <QWheelEvent>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

const auto frameUpdateEvent = static_cast<QEvent::Type>(QEvent::registerEventType());

void requestFrameUpdate(void *widget)
{
    QCoreApplication::postEvent(static_cast<VideoWidget *>(widget), new QEvent(frameUpdateEvent));
}

void *getGlProcAddress(void *context, const char *name)
{
    return reinterpret_cast<void *>(static_cast<QOpenGLContext *>(context)->getProcAddress(name));
}

QPointF rotatedFocus(QPointF point, int degrees)
{
    switch ((degrees % 360 + 360) % 360) {
    case 90: return {1 - point.y(), point.x()};
    case 180: return {1 - point.x(), 1 - point.y()};
    case 270: return {point.y(), 1 - point.x()};
    default: return point;
    }
}

} // namespace

VideoWidget::VideoWidget(mpv_handle *handle, QWidget *parent)
    : QOpenGLWidget(parent), player_(handle)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    clickTimer_.setSingleShot(true);
    connect(&clickTimer_, &QTimer::timeout, this, &VideoWidget::playbackToggleRequested);
    zoomTimer_.setTimerType(Qt::PreciseTimer);
    zoomTimer_.setInterval(12);
    connect(&zoomTimer_, &QTimer::timeout, this, &VideoWidget::advanceZoom);
}

VideoWidget::~VideoWidget()
{
    releaseRenderer();
}

void VideoWidget::setInputMode(InputMode mode)
{
    if (inputMode_ == mode)
        return;
    cancelInput();
    inputMode_ = mode;
}

void VideoWidget::setVideoSize(QSize displaySize, int rotation)
{
    if (displaySize_ == displaySize && rotation_ == rotation)
        return;
    cancelFullViewPreview();
    cancelGestures();
    const auto focus = viewFocus();
    const bool rotationChanged = rotation_ != rotation;
    rotation_ = rotation;
    displaySize_ = displaySize;
    bufferHasFrame_ = false;
    view_.setSizes(size(), displaySize_);
    if (rotationChanged) view_.setFocus(view_.scale(), rotatedFocus(focus, rotation_));
    if (displaySize_.isEmpty())
        hasFrame_ = false;
    emit viewChanged();
    update();
}

void VideoWidget::setVideoTransform(std::optional<quint64> version)
{
    if (videoTransform_ == version) return;
    videoTransform_ = version;
    paintedTransform_.reset();
    if (version) bufferHasFrame_ = false;
    cancelFullViewPreview();
    update();
}

QPointF VideoWidget::viewFocus() const
{
    return rotatedFocus(view_.focus(), -rotation_);
}

VideoWidget::Framing VideoWidget::framing() const
{
    const QSize pixels = size() * devicePixelRatioF();
    return {pixels, displaySize_, pictureBounds(pixels), view_.scale(), rotation_};
}

void VideoWidget::restoreView(double scale, QPointF focus)
{
    cancelGestures();
    view_.setFocus(scale, rotatedFocus(focus, rotation_));
    emit viewChanged();
    update();
}

bool VideoWidget::beginFullViewPreview(FullViewInput input)
{
    if (fullViewPreview_ || !videoTransform_ || !canCaptureTransform(*videoTransform_)) return false;
    fullViewPreview_ = FullViewPreview{input, zoomScale(), viewFocus()};
    restoreView(1, {0.5, 0.5});
    return true;
}

bool VideoWidget::fullViewPreviewActive(FullViewInput input) const
{
    return fullViewPreview_ && fullViewPreview_->input == input;
}

void VideoWidget::endFullViewPreview(FullViewInput input)
{
    if (fullViewPreviewActive(input)) cancelFullViewPreview();
}

void VideoWidget::cancelFullViewPreview()
{
    if (!fullViewPreview_) return;
    const auto saved = *fullViewPreview_;
    fullViewPreview_.reset();
    restoreView(saved.scale, saved.focus);
}

void VideoWidget::centerView()
{
    if (!canCapture()) return;
    cancelFullViewPreview();
    restoreView(view_.scale(), {0.5, 0.5});
}

void VideoWidget::resetView()
{
    cancelFullViewPreview();
    cancelGestures();
    view_.reset();
    emit zoomChanged(view_.scale());
    emit viewChanged();
    update();
}

void VideoWidget::cancelGestures()
{
    zoomTimer_.stop();
    clickTimer_.stop();
    leftPressActive_ = false;
    suppressRelease_ = false;
}

bool VideoWidget::cancelInput()
{
    const bool active = leftPressActive_ || clickTimer_.isActive() || fullViewPreview_.has_value();
    cancelFullViewPreview();
    cancelGestures();
    return active;
}

bool VideoWidget::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
    case QEvent::UngrabMouse:
    case QEvent::Hide:
        cancelGestures();
        break;
    case QEvent::DevicePixelRatioChange:
        // The FBO has a new pixel density; the model stays in widget-local DIP.
        cancelGestures();
        emit viewChanged();
        update();
        break;
    default:
        break;
    }
    return QOpenGLWidget::event(event);
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    cancelGestures();
    if (!event->size().isEmpty()) {
        view_.setSizes(event->size(), displaySize_);
        emit viewChanged();
        if (view_.valid())
            update();
    }
    QOpenGLWidget::resizeEvent(event);
}

void VideoWidget::wheelEvent(QWheelEvent *event)
{
    if (leftPressActive_) {
        event->accept();
        return;
    }
    const double steps = event->pixelDelta().isNull() ? event->angleDelta().y() / 120.0
                                                      : event->pixelDelta().y() / 60.0;
    const bool zooming = inputMode_ == InputMode::Feature || event->modifiers().testFlag(Qt::ControlModifier);
    if (steps == 0 || (zooming && (!hasFrame_ || !view_.valid()))) {
        event->ignore();
        return;
    }
    event->accept();
    if (!zooming) {
        emit volumeStepsRequested(steps);
        return;
    }
    zoomAt(event->position(), steps);
}

void VideoWidget::setZoomScale(double scale)
{
    if (!canCapture() || leftPressActive_ || !std::isfinite(scale)) return;
    cancelFullViewPreview();
    zoomTimer_.stop();
    const double previous = view_.scale();
    view_.zoomAt({width() / 2.0, height() / 2.0}, scale);
    if (view_.scale() == previous) return;
    emit zoomChanged(view_.scale());
    emit viewChanged();
    update();
}

void VideoWidget::zoomSteps(double steps) { zoomAt({width() / 2.0, height() / 2.0}, steps); }

void VideoWidget::zoomAt(QPointF pointer, double steps)
{
    if (!hasFrame_ || !view_.valid() || leftPressActive_) return;
    cancelFullViewPreview();
    const bool continuing = zoomTimer_.isActive() && (zoomTarget_ - view_.scale()) * steps > 0;
    const double base = continuing ? zoomTarget_ : view_.scale();
    const double target = std::clamp(base * std::pow(1.15, steps), 1.0, 8.0);
    if (target == view_.scale()) {
        zoomTimer_.stop();
        return;
    }
    if (continuing && target == zoomTarget_ && pointer == zoomPointer_)
        return;

    zoomStart_ = view_;
    zoomTarget_ = target;
    zoomPointer_ = pointer;
    zoomDeadline_.setRemainingTime(80, Qt::PreciseTimer);
    if (!zoomTimer_.isActive()) {
        sinceZoomFrame_.start();
        zoomTimer_.start();
    }
}

void VideoWidget::advanceZoom()
{
    // Input can retarget faster than the display refreshes. Keep elapsed frame
    // time and extend only the finish deadline when more input arrives.
    const double elapsed = sinceZoomFrame_.restart();
    const double remaining = zoomDeadline_.remainingTime();
    const double progress = remaining > 0 ? elapsed / (elapsed + remaining) : 1.0;
    const double scale = std::lerp(view_.scale(), zoomTarget_, progress);
    view_ = zoomStart_;
    view_.zoomAt(zoomPointer_, scale);
    if (progress == 1)
        zoomTimer_.stop();
    emit zoomChanged(view_.scale());
    emit viewChanged();
    update();
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) cancelGestures();
    if (event->button() != Qt::LeftButton
        || (inputMode_ == InputMode::Feature && (!hasFrame_ || !view_.valid()))) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    if (inputMode_ == InputMode::Feature) cancelFullViewPreview();
    leftPressActive_ = true;
    suppressRelease_ = false;
    pressPosition_ = event->position();
    if (inputMode_ == InputMode::Feature) {
        zoomTimer_.stop();
        dragOrigin_ = view_.topLeft();
    }
    event->accept();
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!leftPressActive_ || !(event->buttons() & Qt::LeftButton)) {
        leftPressActive_ = false;
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }
    if (inputMode_ == InputMode::Feature)
        dragTo(event->position());
    else if (!withinClickDistance(event->position()))
        leftPressActive_ = false;
    event->accept();
}

bool VideoWidget::withinClickDistance(QPointF position) const
{
    return (position - pressPosition_).manhattanLength() <= QApplication::startDragDistance();
}

void VideoWidget::dragTo(QPointF position)
{
    moveViewTo(dragOrigin_ + position - pressPosition_);
}

bool VideoWidget::canPanBy(QPointF delta) const
{
    if (!fullViewPreview_) return view_.canPanBy(delta);
    auto savedView = view_;
    savedView.setFocus(fullViewPreview_->scale, rotatedFocus(fullViewPreview_->focus, rotation_));
    return savedView.canPanBy(delta);
}

void VideoWidget::panBy(QPointF delta)
{
    if (!canCapture() || leftPressActive_) return;
    cancelFullViewPreview();
    zoomTimer_.stop();
    moveViewTo(view_.topLeft() + delta);
}

void VideoWidget::moveViewTo(QPointF position)
{
    auto dragged = view_;
    dragged.moveTo(position);
    if (dragged.topLeft() == view_.topLeft()) return;
    view_ = dragged;
    emit viewChanged();
    update();
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && suppressRelease_) {
        suppressRelease_ = false; event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && leftPressActive_) {
        leftPressActive_ = false;
        event->accept();
        if (inputMode_ == InputMode::Feature)
            dragTo(event->position());
        else if (withinClickDistance(event->position()) && QRectF(rect()).contains(event->position()))
            clickTimer_.start(QApplication::doubleClickInterval());
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) { QOpenGLWidget::mouseDoubleClickEvent(event); return; }
    cancelGestures();
    suppressRelease_ = true;
    emit fullscreenRequested();
    event->accept();
}

void VideoWidget::initializeGL()
{
    auto *glContext = context();
    auto *gl = glContext->functions();
    std::fprintf(stderr, "GL vendor: %s\nGL renderer: %s\nGL version: %s\n",
                 reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)),
                 reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)),
                 reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)));

    char apiType[] = MPV_RENDER_API_TYPE_OPENGL;
    mpv_opengl_init_params glInit{getGlProcAddress, glContext};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, apiType},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    const int result = mpv_render_context_create(&renderer_, player_, params);
    std::fprintf(stderr, "mpv_render_context_create: %d (%s)\n", result, mpv_error_string(result));
    if (result < 0) {
        QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(EXIT_FAILURE); },
                                  Qt::QueuedConnection);
        return;
    }

    ownerContext_ = glContext;
    destructionConnection_ = connect(glContext, &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        releaseRenderer();
        cancelFullViewPreview();
        emit rendererReadyChanged(false);
    }, Qt::DirectConnection);
    mpv_render_context_set_update_callback(renderer_, requestFrameUpdate, this);
    emit rendererReadyChanged(true);
}

void VideoWidget::customEvent(QEvent *event)
{
    if (event->type() != frameUpdateEvent) {
        QOpenGLWidget::customEvent(event);
        return;
    }
    if (isVisible() && !window()->isMinimized()) {
        update();
        return;
    }

    // A minimized widget may receive no paint events. Consume the frame so mpv can advance.
    makeCurrent();
    if (QOpenGLContext::currentContext() != ownerContext_)
        return;
    if (mpv_render_context_update(renderer_) & MPV_RENDER_UPDATE_FRAME) {
        skipFrame();
        hasFrame_ = true;
        bufferHasFrame_ = false;
    }
    doneCurrent();
}

void VideoWidget::skipFrame()
{
    int skip = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_SKIP_RENDERING, &skip},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(renderer_, params);
}

bool VideoWidget::renderFrame(int fbo, QSize pixels)
{
    mpv_opengl_fbo target{fbo, pixels.width(), pixels.height(), 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &target},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    const int result = mpv_render_context_render(renderer_, params);
    if (result < 0)
        std::fprintf(stderr, "mpv_render_context_render: %d (%s)\n", result, mpv_error_string(result));
    return result >= 0;
}

void VideoWidget::prepareVideoBuffer()
{
    if (bufferForSize_ == displaySize_)
        return;
    videoBuffer_.reset();
    bufferHasFrame_ = false;
    bufferForSize_ = displaySize_;
    GLint maxSize = 0;
    context()->functions()->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    QSize pixels = displaySize_;
    if (pixels.width() > maxSize || pixels.height() > maxSize)
        pixels.scale(maxSize, maxSize, Qt::KeepAspectRatio);
    QOpenGLFramebufferObjectFormat format;
    format.setInternalTextureFormat(GL_RGBA8);
    while (!pixels.isEmpty()) {
        auto candidate = std::make_unique<QOpenGLFramebufferObject>(pixels, format);
        if (candidate->isValid()) {
            videoBuffer_ = std::move(candidate);
            break;
        }
        pixels = {pixels.width() / 2, pixels.height() / 2};
    }
    if (!videoBuffer_)
        std::fputs("Cannot allocate a video texture; displaying fit-to-window video\n", stderr);
    else if (pixels != displaySize_)
        std::fprintf(stderr, "Video texture reduced to %dx%d\n", pixels.width(), pixels.height());
}

QRect VideoWidget::pictureBounds(QSize pixels) const
{
    if (!view_.valid() || size().isEmpty()) return {};
    const auto origin = view_.topLeft();
    const auto frame = view_.frameSize();
    const double dx = double(pixels.width()) / width();
    const double dy = double(pixels.height()) / height();
    const int left = qRound(origin.x() * dx);
    const int top = qRound(origin.y() * dy);
    return {left, top, qRound((origin.x() + frame.width()) * dx) - left,
                       qRound((origin.y() + frame.height()) * dy) - top};
}

void VideoWidget::paintGL()
{
    paintedTransform_.reset();
    auto *gl = context()->extraFunctions();
    GLint viewport[4];
    gl->glGetIntegerv(GL_VIEWPORT, viewport);
    const QSize pixels(viewport[2], viewport[3]);
    const GLuint destination = defaultFramebufferObject();
    const bool newFrame = renderer_ && (mpv_render_context_update(renderer_) & MPV_RENDER_UPDATE_FRAME);
    if (newFrame)
        hasFrame_ = true;
    if (!hasFrame_ || !view_.valid()) {
        if (newFrame)
            skipFrame();
        gl->glClearColor(0, 0, 0, 1);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    prepareVideoBuffer();
    if (!videoBuffer_) {
        if (renderFrame(static_cast<int>(destination), pixels)) paintedTransform_ = videoTransform_;
        return;
    }
    const QSize source = videoBuffer_->size();
    if (newFrame || !bufferHasFrame_)
        bufferHasFrame_ = renderFrame(static_cast<int>(videoBuffer_->handle()), source);

    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0, 0, 0, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    if (bufferHasFrame_) {
        // Composite one complete view of the full-resolution frame. GL clips
        // the destination at the viewport edges while preserving its scale.
        const auto picture = pictureBounds(pixels);
        const int top = pixels.height() - picture.y();
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, videoBuffer_->handle());
        gl->glBlitFramebuffer(0, 0, source.width(), source.height(),
            picture.x(), top - picture.height(), picture.x() + picture.width(), top, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        paintedTransform_ = videoTransform_;
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, destination);
    gl->glViewport(0, 0, pixels.width(), pixels.height());
}

void VideoWidget::releaseRenderer()
{
    cancelGestures();
    paintedTransform_.reset();
    videoTransform_.reset();
    if (!renderer_)
        return;

    disconnect(destructionConnection_);
    makeCurrent();
    if (QOpenGLContext::currentContext() != ownerContext_)
        qFatal("Cannot make the libmpv renderer's OpenGL context current for cleanup");

    // libmpv serializes callback replacement with invocation. No more events can be
    // posted for this renderer once unregister returns.
    mpv_render_context_set_update_callback(renderer_, nullptr, nullptr);
    QCoreApplication::removePostedEvents(this, frameUpdateEvent);
    mpv_render_context_free(renderer_);
    videoBuffer_.reset();
    bufferForSize_ = {};
    bufferHasFrame_ = false;
    renderer_ = nullptr;
    ownerContext_ = nullptr;
    hasFrame_ = false;
    doneCurrent();
}
