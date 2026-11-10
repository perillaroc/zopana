#pragma once
#include <QObject>
#include <QPointer>
#include <memory>
class QSplitter;
class QWidget;
class QVBoxLayout;
class PlaylistPanel;
class PlaylistWindow;
class PlaylistHost final : public QObject
{
    Q_OBJECT
public:
    PlaylistHost(QWidget &main, QWidget &central, QSplitter &splitter, PlaylistPanel *panel);
    ~PlaylistHost() override;
    bool detached() const { return detached_; }
    bool visible() const { return fullscreen_ && !detached_ ? fullscreenVisible_ : visible_; }
    bool overlayVisible() const;
    QWidget *window() const;
    void setDetached(bool detached);
    void setVisible(bool visible, bool activate = true);
    void toggle();
    void setFullscreen(bool fullscreen);
    bool dismissOverlay();
    void shutdown();
signals:
    void changed();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void arrange(bool activate = false);
    QWidget &main_, &central_;
    QSplitter &splitter_;
    QPointer<PlaylistPanel> panel_;
    QWidget *dock_, *scrim_, *overlay_;
    std::unique_ptr<PlaylistWindow> window_;
    QPointer<QWidget> previousFocus_;
    bool detached_ = false, visible_ = false, fullscreen_ = false, fullscreenVisible_ = false;
    int dockWidth_ = 320;
};
