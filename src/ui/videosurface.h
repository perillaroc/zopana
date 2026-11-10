#pragma once

#include <QWidget>
#include <QTimer>

class QAction;
class QLabel;
class QToolButton;
class QScrollArea;
class VideoWidget;

class VideoSurface final : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(VideoWidget *video, QWidget *parent = nullptr);
    void present(const QString &title, const QString &body, QAction *primary, QAction *secondary = nullptr);
    void showVideo();
    void setControls(QWidget *controls);
    void setFullscreen(bool fullscreen);
    void showOsd(const QString &kind, const QString &text, int milliseconds = 1200);
    bool dismissOsd();
    void showCaptureSaved(const QString &path, QAction *openFolder);
    void setRecentActions(const QList<QAction *> &actions);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrange();
    VideoWidget *video_;
    QWidget *card_;
    QLabel *title_, *body_;
    QToolButton *primary_, *secondary_;
    QWidget *recent_;
    QScrollArea *recentScroll_;
    QWidget *controls_ = nullptr;
    QLabel *osd_;
    QToolButton *captureFolder_;
    QTimer osdTimer_;
    bool fullscreen_ = false;
};
