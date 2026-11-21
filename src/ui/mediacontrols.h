#pragma once
#include <QObject>
#include <QVariant>
class Player;
class PlayerActions;
class VideoWidget;
class VideoSurface;
class ControlBar;
class QMainWindow;
class QMenu;
class QDoubleSpinBox;
class CaptureService;

class MediaControls final : public QObject
{
    Q_OBJECT
public:
    MediaControls(Player &player, PlayerActions &actions, VideoWidget &video, VideoSurface &surface, ControlBar &bar, QMainWindow &window);
    void refresh();
    QMenu &chapterMenu() const { return *chaptersMenu_; }
    void setAlwaysOnTop(bool enabled);
    QVariantMap information() const;
signals:
    void alwaysOnTopChanged();
private:
    void updateVideoActions();
    void updateFrameActions();
    void updateLoopActions();
    void rebuildChapters();
    void updateSeekNavigation();
    void jumpToChapter(qsizetype chapter, double origin);
    void tracksMenu(QMenu *menu, const QString &type);
    void loadSubtitle();
    void showInformation();
    QDoubleSpinBox *numberControl(QMenu *menu, const QString &property, const QString &name, double minimum, double maximum, const QString &description);
    Player &player_;
    PlayerActions &actions_;
    VideoWidget &video_;
    VideoSurface &surface_;
    ControlBar &bar_;
    QMainWindow &window_;
    CaptureService *captures_;
    QMenu *audioTracks_, *subtitleTracks_, *rateMenu_, *chaptersMenu_;
    QDoubleSpinBox *audioDelay_, *subDelay_, *subScale_;
    bool refreshPending_ = false, captureReady_ = false;
    qsizetype currentChapter_ = -1;
};
