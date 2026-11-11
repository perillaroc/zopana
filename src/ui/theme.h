#pragma once

#include <QAbstractNativeEventFilter>
#include <QIcon>
#include <QObject>
#include <QPalette>

class QApplication;

namespace Theme {
// Keep colors in one place; QSS refers to palette roles rather than copies.
inline constexpr auto Window = "#15181d";
inline constexpr auto Surface = "#1d222a";
inline constexpr auto Raised = "#272e38";
inline constexpr auto Border = "#343d4a";
inline constexpr auto Text = "#edf1f7";
inline constexpr auto Secondary = "#b3bdcc";
inline constexpr auto Accent = "#6ea8ff";
inline constexpr auto AccentText = "#152235";

QIcon icon(const QString &name, const QPalette &palette, bool primary = false);

class Controller final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit Controller(QApplication &app, QObject *parent = nullptr);
    ~Controller() override;
    void apply(bool highContrast);
    bool highContrast() const { return highContrast_; }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

signals:
    void applied();

private:
    QApplication &app_;
    bool highContrast_ = false;
    bool pending_ = false;
};
}
