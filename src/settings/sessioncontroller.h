#pragma once
#include "settingsstore.h"
#include "historystore.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
class Player;
class MainWindow;
class PlayerActions;
class PlaylistController;
class PlaylistHost;
class VideoSurface;
class VideoWidget;
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
    void flushProgress(bool ended=false);
    void saveWindows();
    void showSettings();
    void ensureVisibleWindows();
    bool resumePending() const { return awaitingResume_; }
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
    void tryResume();
    void dismissResume();
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
    QMenu *recent_;
    QWidget *prompt_;
    QLabel *promptText_;
    QTimer progressTimer_,settingsTimer_,resumeTimer_,recentTimer_;
    std::optional<HistoryEntry> resumeEntry_;
    std::optional<double> resumeSeek_;
    QString currentPath_;
    QSet<QString> suppressedPaths_;
    bool recordCurrent_=false,awaitingResume_=false,restoring_=true,settingsErrorReported_=false,choosing_=false;
    bool settingsNeedWrite_=false;
    QPointer<QWidget> settingsDialog_;
};
