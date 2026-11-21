#pragma once

#include <QAbstractNativeEventFilter>
#include <QIcon>
#include <QObject>
#include <QPalette>

class QApplication;

namespace Theme {
// Keep colors in one place; QSS refers to palette roles rather than copies.
inline constexpr auto Window = "#211a2b";
inline constexpr auto Surface = "#30243e";
inline constexpr auto Raised = "#433153";
inline constexpr auto Border = "#60506e";
inline constexpr auto Text = "#f5effb";
inline constexpr auto Secondary = "#cbbcd9";
inline constexpr auto Accent = "#dbbaff";
inline constexpr auto AccentText = "#37264e";

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
