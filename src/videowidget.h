#pragma once

#include <QOpenGLWidget>

struct mpv_handle;
struct mpv_render_context;

class VideoWidget final : public QOpenGLWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(mpv_handle *handle, QWidget *parent = nullptr);
    ~VideoWidget() override;

    bool rendererReady() const { return renderer_ != nullptr; }

signals:
    void rendererReadyChanged(bool ready);

protected:
    void initializeGL() override;
    void paintGL() override;
    void customEvent(QEvent *event) override;

private:
    void releaseRenderer();

    mpv_handle *player_;
    mpv_render_context *renderer_ = nullptr;
    QOpenGLContext *ownerContext_ = nullptr;
    QMetaObject::Connection destructionConnection_;
    bool hasFrame_ = false;
};
