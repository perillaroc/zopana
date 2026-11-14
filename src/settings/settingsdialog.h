#pragma once
#include "settingsstore.h"
#include <QDialog>
#include <QSet>
#include <functional>
class PlayerActions;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;
class QTabWidget;

struct SettingsEdits {
    QSet<QString> shortcuts;
    bool captureDirectory = false, audioLanguages = false, subtitleLanguages = false;
    bool recordHistory = false, resumePolicy = false;
    bool seekSmallSeconds = false, seekMediumSeconds = false, seekLargeSeconds = false;
    bool skipEnabled = false, introEnabled = false, outroEnabled = false;
    bool introSeconds = false, outroSeconds = false;
    bool resetAll = false;
};

class SettingsDialog final : public QDialog
{
public:
    SettingsDialog(const Preferences &preferences, PlayerActions &actions,
                   std::function<bool(const Preferences &, const SettingsEdits &)> apply,
                   std::function<void()> clear, QWidget *parent);
    void showSkipPage();
private:
    void apply(bool close);
    void resetPage();
    void resetAll();
    void resetShortcuts();
    void resetSkip();
    void populate();
    void select();
    void bind();
    Preferences draft_;
    SettingsEdits edits_;
    PlayerActions &actions_;
    std::function<bool(const Preferences &, const SettingsEdits &)> apply_;
    QTabWidget *tabs_;
    QWidget *general_, *skip_;
    QLineEdit *capture_,*audio_,*sub_,*filter_;
    QCheckBox *history_;
    QComboBox *resume_;
    QSpinBox *seekSmall_, *seekMedium_, *seekLarge_;
    QCheckBox *skipEnabled_, *introEnabled_, *outroEnabled_;
    QSpinBox *introSeconds_, *outroSeconds_;
    QTableWidget *table_;
    QKeySequenceEdit *first_,*second_;
    QLabel *message_;
};
