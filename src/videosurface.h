#pragma once

#include <QWidget>

class QAction;
class QLabel;
class QToolButton;
class VideoWidget;

class VideoSurface final : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(VideoWidget *video, QWidget *parent = nullptr);
    void present(const QString &title, const QString &body, QAction *primary, QAction *secondary = nullptr);
    void showVideo();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrange();
    VideoWidget *video_;
    QWidget *card_;
    QLabel *title_, *body_;
    QToolButton *primary_, *secondary_;
};
