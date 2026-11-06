#include "videowidget.h"

#include <QCoreApplication>
#include <QEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

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
}

VideoWidget::~VideoWidget()
{
    releaseRenderer();
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
        int skip = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_SKIP_RENDERING, &skip},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(renderer_, params);
        hasFrame_ = true;
    }
    doneCurrent();
}

void VideoWidget::paintGL()
{
    auto *gl = context()->functions();
    if (renderer_ && (mpv_render_context_update(renderer_) & MPV_RENDER_UPDATE_FRAME))
        hasFrame_ = true;
    if (!hasFrame_) {
        gl->glClearColor(32.0f / 255, 64.0f / 255, 80.0f / 255, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    // Qt sets the viewport to the current backing FBO's physical size before paintGL().
    GLint viewport[4];
    gl->glGetIntegerv(GL_VIEWPORT, viewport);
    mpv_opengl_fbo target{static_cast<int>(defaultFramebufferObject()), viewport[2], viewport[3], 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &target},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    // Also render on Qt-initiated repaints: mpv redraws its last frame while paused.
    const int result = mpv_render_context_render(renderer_, params);
    if (result < 0)
        std::fprintf(stderr, "mpv_render_context_render: %d (%s)\n", result, mpv_error_string(result));
}

void VideoWidget::releaseRenderer()
{
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
    renderer_ = nullptr;
    ownerContext_ = nullptr;
    hasFrame_ = false;
    doneCurrent();
}
