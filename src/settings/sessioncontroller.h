#pragma once
#include "settingsstore.h"
#include "historystore.h"
#include "player.h"
#include "skippolicy.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
class MainWindow;
class PlayerActions;
class PlaylistController;
class PlaylistHost;
class VideoSurface;
class VideoWidget;
class ControlBar;
class SettingsDialog;
struct SettingsEdits;
class QMenu;
class QLabel;
class SessionController final : public QObject
{
    Q_OBJECT
public:
    SessionController(Player &player,MainWindow &window,SettingsStore &settings,HistoryStore &history);
    const Preferences &preferences() const { return preferences_; }
    bool applyPreferences(Preferences preferences);
    void clearHistory();
    void flushProgress();
    void saveWindows();
    void showSettings();
    void ensureVisibleWindows();
    bool resumePending() const { return startState_ == StartState::Deciding && resumeEntry_.has_value(); }
    void chooseResume(bool resume);
protected:
    bool eventFilter(QObject *object,QEvent *event) override;
private:
    void captureRuntime(Preferences &preferences) const;
    void scheduleSave();
    void applySettings();
    void applyPlaybackSettings(const Preferences &preferences);
    bool applyDialogChanges(const Preferences &draft, const SettingsEdits &edits);
    bool commitPreferences(Preferences preferences, bool resetAll);
    void rebuildRecent();
    void prepareFile();
    void updateSkipSettings();
    void updateSkipBounds();
    void onPlaybackAdvanced(const Player::PlaybackIdentity &playback, double before, double position);
    void onPlaybackCompleted(const Player::PlaybackIdentity &playback);
    bool recordProgress(bool ended);
    void tryStart();
    void startAt(double seconds, Player::StartPosition reason);
    void discardResume();
    void releaseStart(const QString &message = {});
    void resetStart();
    void hideResumePrompt();
    void armStartTimer(int milliseconds);
    void startTimedOut();
    void showSettingsPage(bool skipPage);
    void arrangePrompt();
    void settingsError(const QString &message, bool runtimeChanged = false);
    Player &player_;
    MainWindow &window_;
    SettingsStore &settings_;
    HistoryStore &history_;
    Preferences preferences_;
    Preferences savedPreferences_;
    PlayerActions *actions_;
    PlaylistController *playlist_;
    PlaylistHost *host_;
    VideoSurface *surface_;
    VideoWidget *video_;
    ControlBar *controls_;
    QMenu *recent_;
    QWidget *prompt_;
    QLabel *promptText_;
    enum class StartState { Ready, Deciding, Seeking };
    StartState startState_ = StartState::Ready;
    Player::PlaybackIdentity startPlayback_;
    Player::StartPosition startReason_ = Player::StartPosition::Beginning;
    double startTarget_ = 0;
    QTimer progressTimer_,settingsTimer_,startTimer_,recentTimer_;
    std::optional<HistoryEntry> resumeEntry_;
    SkipPolicy skip_;
    bool loopWasActive_ = false;
    QString currentPath_;
    QSet<QString> suppressedPaths_;
    bool recordCurrent_=false,restoring_=true,settingsErrorReported_=false;
    bool settingsNeedWrite_=false;
    QPointer<SettingsDialog> settingsDialog_;
};
