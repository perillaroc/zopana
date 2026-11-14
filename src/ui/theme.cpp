#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QFocusEvent>
#include <QIconEngine>
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QWidget>
#include <windows.h>

static void initializePlayerResources()
{
    Q_INIT_RESOURCE(player);
}

namespace {
bool systemHighContrast()
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
        && (contrast.dwFlags & HCF_HIGHCONTRASTON);
}

QColor systemColor(int index)
{
    const COLORREF color = GetSysColor(index);
    return {GetRValue(color), GetGValue(color), GetBValue(color)};
}

class SvgIcon final : public QIconEngine
{
public:
    SvgIcon(QByteArray data, QPalette palette, bool primary)
        : data_(std::move(data)), palette_(std::move(palette)), primary_(primary) {}
    QIconEngine *clone() const override { return new SvgIcon(*this); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const auto role = primary_ || mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        const QColor color = mode == QIcon::Disabled ? palette_.color(QPalette::Disabled, QPalette::ButtonText) : palette_.color(role);
        auto svg = data_;
        svg.replace("currentColor", color.name().toUtf8());
        QSvgRenderer renderer(svg);
        renderer.render(painter, rect);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap image(size);
        image.setDevicePixelRatio(scale);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(), size / scale), mode, state);
        return image;
    }
private:
    QByteArray data_;
    QPalette palette_;
    bool primary_;
};

const char stylesheet[] = R"(
QMainWindow, QWidget#controlBar, QWidget#playerTopBar { background:palette(button); }
QMenu::item:selected { background:palette(highlight); color:palette(highlighted-text); }
QMenu { background:palette(button); border:1px solid palette(mid); padding:5px; }
QMenu::item { padding:7px 24px 7px 28px; border-radius:3px; }
QMenu::item:disabled { color:palette(disabled,button-text); }
QMenu::separator { height:1px; background:palette(mid); margin:5px; }
QToolButton { color:palette(button-text); border:1px solid transparent; border-bottom-width:2px; border-radius:5px; padding:6px; }
QToolButton[playlistOrderControl="true"] { border-color:palette(mid); }
QToolButton:hover { background:palette(alternate-base); }
QToolButton:pressed, QToolButton:checked { background:palette(mid); }
QToolButton[keyboardFocus="true"]:focus { border-bottom-color:palette(highlight); }
QToolButton:disabled { color:palette(disabled,button-text); }
QToolButton::menu-indicator { image:none; }
QToolButton#playPauseButton, QToolButton#cardPrimary { background:palette(highlight); color:palette(highlighted-text); }
QToolButton#playPauseButton:disabled, QToolButton#cardPrimary:disabled { background:palette(alternate-base); }
QToolButton#playPauseButton[keyboardFocus="true"]:focus, QToolButton#cardPrimary[keyboardFocus="true"]:focus { border-bottom-color:palette(highlighted-text); }
QToolButton#windowCloseButton:hover { background:#a82b35; color:white; }
QLabel#zoomLabel, QLabel#rateLabel, QLabel#cardBody { color:palette(placeholder-text); }
QListView#playlistView { outline:0; selection-background-color:palette(mid); selection-color:palette(text); }
QWidget#stateCard { background:palette(window); border:1px solid palette(mid); border-radius:8px; }
QLabel#playerOsd { background:palette(button); color:palette(text); border:1px solid palette(mid); border-radius:6px; }
QLabel#cardTitle { font-size:20px; font-weight:600; }
QSlider { border:2px solid transparent; outline:0; }
QSlider::groove:horizontal { height:4px; background:palette(mid); border-radius:2px; }
QSlider::sub-page:horizontal { background:palette(highlight); border-radius:2px; }
QSlider::handle:horizontal { width:12px; margin:-4px 0; background:palette(highlight); border-radius:6px; }
QSlider[keyboardFocus="true"]::handle:horizontal:focus { background:palette(text); }
QSlider::handle:horizontal:disabled { background:palette(disabled,button-text); }
QToolTip { background:palette(button); color:palette(text); border:1px solid palette(mid); padding:5px; }
)";
}

namespace Theme {
QIcon icon(const QString &name, const QPalette &palette, bool primary)
{
    QFile file(":/icons/lucide/" + name + ".svg");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QIcon(new SvgIcon(file.readAll(), palette, primary && !qApp->property("playerHighContrast").toBool()));
}

Controller::Controller(QApplication &app, QObject *parent) : QObject(parent), app_(app)
{
    static const bool initialized = [] { initializePlayerResources(); return true; }();
    Q_UNUSED(initialized);
    app_.installNativeEventFilter(this);
    app_.installEventFilter(this);
    apply(systemHighContrast());
}

Controller::~Controller() { app_.removeEventFilter(this); app_.removeNativeEventFilter(this); }

bool Controller::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::MouseButtonPress:
    case QEvent::KeyPress: {
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || (!qobject_cast<QToolButton *>(widget) && !qobject_cast<QSlider *>(widget))) break;
        bool keyboardFocus = event->type() == QEvent::KeyPress;
        if (event->type() == QEvent::FocusIn) {
            const auto reason = static_cast<QFocusEvent *>(event)->reason();
            keyboardFocus = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
        }
        if (widget->property("keyboardFocus").toBool() != keyboardFocus) {
            widget->setProperty("keyboardFocus", keyboardFocus);
            if (!highContrast_) { widget->style()->unpolish(widget); widget->style()->polish(widget); widget->update(); }
        }
        break;
    }
    default: break;
    }
    return false;
}

void Controller::apply(bool highContrast)
{
    highContrast_ = highContrast;
    app_.setProperty("playerHighContrast", highContrast);
    // Remove palette-dependent style caches before changing their inputs.
    app_.setStyleSheet({});
    QPalette palette;
    if (highContrast) {
        app_.styleHints()->unsetColorScheme();
        for (auto [role, index] : {std::pair{QPalette::Window, COLOR_WINDOW}, {QPalette::WindowText, COLOR_WINDOWTEXT},
                {QPalette::Base, COLOR_WINDOW}, {QPalette::Text, COLOR_WINDOWTEXT},
                {QPalette::Button, COLOR_BTNFACE}, {QPalette::ButtonText, COLOR_BTNTEXT},
                {QPalette::Highlight, COLOR_HIGHLIGHT}, {QPalette::HighlightedText, COLOR_HIGHLIGHTTEXT},
                {QPalette::ToolTipBase, COLOR_INFOBK}, {QPalette::ToolTipText, COLOR_INFOTEXT}})
            palette.setColor(role, systemColor(index));
        palette.setColor(QPalette::Disabled, QPalette::Text, systemColor(COLOR_GRAYTEXT));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, systemColor(COLOR_GRAYTEXT));
    } else {
        app_.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        for (auto [role, color] : {std::pair{QPalette::Window, Window}, {QPalette::WindowText, Text},
                {QPalette::Base, Window}, {QPalette::AlternateBase, Raised}, {QPalette::Button, Surface},
                {QPalette::ButtonText, Text}, {QPalette::Text, Text}, {QPalette::Mid, Border},
                {QPalette::Highlight, Accent}, {QPalette::HighlightedText, AccentText},
                {QPalette::PlaceholderText, Secondary}, {QPalette::ToolTipBase, Surface}, {QPalette::ToolTipText, Text}})
            palette.setColor(role, QColor(color));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(Secondary));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(Secondary));
    }
    app_.setPalette(palette);
    // Stylesheets leave resolved widget palettes behind when unpolished.
    // Reset them as well, including menus and existing modal dialogs.
    for (auto *widget : app_.allWidgets()) widget->setPalette(palette);
    if (!highContrast) app_.setStyleSheet(QString::fromLatin1(stylesheet));
    emit applied();
}

bool Controller::nativeEventFilter(const QByteArray &, void *message, qintptr *)
{
    const auto *event = static_cast<MSG *>(message);
    if ((event->message == WM_SETTINGCHANGE || event->message == WM_SYSCOLORCHANGE) && !pending_) {
        pending_ = true;
        QTimer::singleShot(0, this, [this] {
            pending_ = false;
            const bool contrast = systemHighContrast();
            if (contrast || contrast != highContrast_) apply(contrast);
        });
    }
    return false;
}
}
