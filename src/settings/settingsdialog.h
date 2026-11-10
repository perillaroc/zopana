#pragma once
#include "settingsstore.h"
#include <QDialog>
#include <functional>
class PlayerActions;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QKeySequenceEdit;
class QLabel;
class SettingsDialog final : public QDialog
{
public:
    SettingsDialog(Preferences preferences,PlayerActions &actions,std::function<bool(const Preferences &)> apply,std::function<void()> clear,QWidget *parent);
private:
    void apply(bool close);
    void populate();
    void select();
    void bind();
    Preferences draft_;
    PlayerActions &actions_;
    std::function<bool(const Preferences &)> apply_;
    QLineEdit *capture_,*audio_,*sub_,*filter_;
    QCheckBox *history_;
    QComboBox *resume_;
    QTableWidget *table_;
    QKeySequenceEdit *first_,*second_;
    QLabel *message_;
};
