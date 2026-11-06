#include <QApplication>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

void logApiVersion(const char *source, unsigned long version)
{
    std::fprintf(stderr, "libmpv client API (%s): %lu.%lu\n", source,
                 version >> 16, version & 0xffffUL);
}

void logMpvResult(const char *operation, int result)
{
    std::fprintf(stderr, "%s: %d (%s)\n", operation, result, mpv_error_string(result));
}

void *getGlProcAddress(void *context, const char *name)
{
    return reinterpret_cast<void *>(static_cast<QOpenGLContext *>(context)->getProcAddress(name));
}

class VideoWidget final : public QOpenGLWidget
{
public:
    explicit VideoWidget(mpv_handle *player) : player_(player) {}

    ~VideoWidget() override { releaseRenderer(); }

protected:
    void initializeGL() override
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
        logMpvResult("mpv_render_context_create", result);
        if (result < 0) {
            // show() can initialize GL before the application's event loop starts.
            QMetaObject::invokeMethod(qApp, [] { QCoreApplication::exit(EXIT_FAILURE); },
                                      Qt::QueuedConnection);
            return;
        }

        ownerContext_ = glContext;
        connect(ownerContext_, &QOpenGLContext::aboutToBeDestroyed, this,
                &VideoWidget::releaseRenderer, Qt::DirectConnection);
    }

    void paintGL() override
    {
        auto *gl = context()->functions();
        gl->glClearColor(32.0f / 255, 64.0f / 255, 80.0f / 255, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }

private:
    void releaseRenderer()
    {
        if (!renderer_)
            return;

        disconnect(ownerContext_, &QOpenGLContext::aboutToBeDestroyed, this,
                   &VideoWidget::releaseRenderer);
        makeCurrent();
        if (QOpenGLContext::currentContext() != ownerContext_)
            qFatal("Cannot make the libmpv renderer's OpenGL context current for cleanup");
        mpv_render_context_free(renderer_);
        renderer_ = nullptr;
        ownerContext_ = nullptr;
        doneCurrent();
    }

    mpv_handle *player_;
    mpv_render_context *renderer_ = nullptr;
    QOpenGLContext *ownerContext_ = nullptr;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    std::fprintf(stderr, "Qt runtime: %s\n", qVersion());
    logApiVersion("headers", MPV_CLIENT_API_VERSION);
    logApiVersion("runtime", mpv_client_api_version());

    std::unique_ptr<mpv_handle, decltype(&mpv_destroy)> player(mpv_create(), &mpv_destroy);
    if (!player) {
        std::fputs("mpv_create: could not allocate a player handle\n", stderr);
        return EXIT_FAILURE;
    }
    int result = mpv_set_option_string(player.get(), "vo", "libmpv");
    if (result < 0) {
        logMpvResult("mpv_set_option_string(vo=libmpv)", result);
        return EXIT_FAILURE;
    }
    result = mpv_initialize(player.get());
    if (result < 0) {
        logMpvResult("mpv_initialize", result);
        return EXIT_FAILURE;
    }

    VideoWidget window(player.get());
    window.setWindowTitle(QStringLiteral("Zopana"));
    window.resize(960, 540);
    window.show();
    if (!window.isValid()) {
        std::fputs("QOpenGLWidget failed to create its OpenGL context\n", stderr);
        return EXIT_FAILURE;
    }
    return app.exec();
}
