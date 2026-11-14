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
class QTabWidget;

struct SettingsEdits {
    QSet<QString> shortcuts;
    bool captureDirectory = false, audioLanguages = false, subtitleLanguages = false;
    bool recordHistory = false, resumePolicy = false;
    bool resetAll = false;
};

class SettingsDialog final : public QDialog
{
public:
    SettingsDialog(const Preferences &preferences, PlayerActions &actions,
                   std::function<bool(const Preferences &, const SettingsEdits &)> apply,
                   std::function<void()> clear, QWidget *parent);
private:
    void apply(bool close);
    void resetPage();
    void resetAll();
    void resetShortcuts();
    void populate();
    void select();
    void bind();
    Preferences draft_;
    SettingsEdits edits_;
    PlayerActions &actions_;
    std::function<bool(const Preferences &, const SettingsEdits &)> apply_;
    QTabWidget *tabs_;
    QWidget *general_;
    QLineEdit *capture_,*audio_,*sub_,*filter_;
    QCheckBox *history_;
    QComboBox *resume_;
    QTableWidget *table_;
    QKeySequenceEdit *first_,*second_;
    QLabel *message_;
};
