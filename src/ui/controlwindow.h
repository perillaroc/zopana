#pragma once

#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <array>
#include <optional>

class QAction;
class QLabel;
class QMenu;
class QSlider;
class QTabWidget;
class QToolButton;
class Player;
class PlayerActions;
class VideoWidget;

class ControlWindow final : public QWidget
{
    Q_OBJECT
public:
    ControlWindow(QWidget &main, Player &player, PlayerActions &actions, VideoWidget &video, QMenu &chapters);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QWidget *createPlaybackPage(PlayerActions &actions, QMenu &chapters);
    QWidget *createFramingPage(PlayerActions &actions);
    void syncVisibility(bool activate = false);
    void ensureOnScreen();
    void updateTime();
    void updatePlayback();
    bool viewAvailable() const;
    void updateView();
    void cancelPan();
    bool handlePanPad(QEvent *event);

    QWidget &main_;
    Player &player_;
    VideoWidget &video_;
    QAction *toggle_;
    QPointer<QWidget> returnFocus_;
    QTabWidget *tabs_;
    QLabel *current_, *duration_, *rate_, *loopA_, *loopB_, *notice_;
    QLabel *zoomValue_, *panPad_ = nullptr;
    QSlider *zoom_;
    QWidget *framingPage_;
    std::array<QToolButton *, 4> panButtons_{};
    std::optional<QPointF> dragPosition_;
    QTimer noticeTimer_;
    bool placed_ = false, viewReady_ = false;
};
