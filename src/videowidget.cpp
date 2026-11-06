#include "videowidget.h"

#include <QCoreApplication>
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

} // namespace

VideoWidget::VideoWidget(mpv_handle *handle, QWidget *parent)
    : QOpenGLWidget(parent), player_(handle)
{
    setFocusPolicy(Qt::ClickFocus);
    zoomTimer_.setTimerType(Qt::PreciseTimer);
    zoomTimer_.setInterval(12);
    connect(&zoomTimer_, &QTimer::timeout, this, &VideoWidget::advanceZoom);
}

VideoWidget::~VideoWidget()
{
    releaseRenderer();
}

void VideoWidget::setVideoSize(QSize displaySize)
{
    if (displaySize_ == displaySize)
        return;
    cancelGestures();
    displaySize_ = displaySize;
    bufferHasFrame_ = false;
    view_.setSizes(size(), displaySize_);
    if (displaySize_.isEmpty())
        hasFrame_ = false;
    update();
}

void VideoWidget::resetView()
{
    cancelGestures();
    view_.reset();
    update();
}

void VideoWidget::cancelGestures()
{
    zoomTimer_.stop();
    dragging_ = false;
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
        if (view_.valid())
            update();
    }
    QOpenGLWidget::resizeEvent(event);
}

void VideoWidget::wheelEvent(QWheelEvent *event)
{
    if (dragging_) {
        event->accept();
        return;
    }
    const double steps = event->pixelDelta().isNull() ? event->angleDelta().y() / 120.0
                                                      : event->pixelDelta().y() / 60.0;
    if (!hasFrame_ || !view_.valid() || steps == 0) {
        event->ignore();
        return;
    }
    event->accept();
    const bool continuing = zoomTimer_.isActive() && (zoomTarget_ - view_.scale()) * steps > 0;
    const double base = continuing ? zoomTarget_ : view_.scale();
    const double target = std::clamp(base * std::pow(1.15, steps), 1.0, 8.0);
    if (target == view_.scale()) {
        zoomTimer_.stop();
        return;
    }
    if (continuing && target == zoomTarget_ && event->position() == zoomPointer_)
        return;

    zoomStart_ = view_;
    zoomTarget_ = target;
    zoomPointer_ = event->position();
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
    update();
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !hasFrame_ || !view_.valid()) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    zoomTimer_.stop();
    dragging_ = true;
    dragPointer_ = event->position();
    dragOrigin_ = view_.topLeft();
    event->accept();
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging_ || !(event->buttons() & Qt::LeftButton)) {
        dragging_ = false;
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }
    dragTo(event->position());
    event->accept();
}

void VideoWidget::dragTo(QPointF position)
{
    const auto previous = view_.topLeft();
    view_.moveTo(dragOrigin_ + position - dragPointer_);
    if (view_.topLeft() != previous)
        update();
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && dragging_) {
        dragTo(event->position());
        dragging_ = false;
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
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

void VideoWidget::paintGL()
{
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
        gl->glClearColor(32.0f / 255, 64.0f / 255, 80.0f / 255, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    prepareVideoBuffer();
    if (!videoBuffer_) {
        renderFrame(static_cast<int>(destination), pixels);
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
        const auto origin = view_.topLeft();
        const auto frame = view_.frameSize();
        const double dx = double(pixels.width()) / width();
        const double dy = double(pixels.height()) / height();
        const int left = qRound(origin.x() * dx);
        const int right = qRound((origin.x() + frame.width()) * dx);
        const int bottom = pixels.height() - qRound((origin.y() + frame.height()) * dy);
        const int top = pixels.height() - qRound(origin.y() * dy);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, videoBuffer_->handle());
        gl->glBlitFramebuffer(0, 0, source.width(), source.height(), left, bottom, right, top,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    gl->glBindFramebuffer(GL_FRAMEBUFFER, destination);
    gl->glViewport(0, 0, pixels.width(), pixels.height());
}

void VideoWidget::releaseRenderer()
{
    cancelGestures();
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
