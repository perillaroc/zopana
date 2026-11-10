#include "playbackdialogs.h"
#include "playeractions.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace PlaybackDialogs {
QString timestamp(double seconds)
{
    const auto milliseconds = qMax<qint64>(0, qRound64(seconds * 1000));
    const auto whole = milliseconds / 1000;
    return QStringLiteral("%1:%2:%3.%4").arg(whole / 3600, 2, 10, QLatin1Char('0'))
        .arg((whole / 60) % 60, 2, 10, QLatin1Char('0')).arg(whole % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

std::optional<double> parseTime(const QString &text)
{
    static const QRegularExpression syntax(QStringLiteral(R"(^\d+(?::\d{1,2}){0,2}(?:\.\d{1,3})?$)"));
    const auto clean = text.trimmed();
    if (!syntax.match(clean).hasMatch()) return {};
    const auto parts = clean.split(':');
    double total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false; const double value = parts[i].toDouble(&ok);
        if (!ok || !std::isfinite(value) || (i > 0 && value >= 60)) return {};
        total = total * 60 + value;
    }
    return std::isfinite(total) ? std::optional(total) : std::nullopt;
}

std::optional<double> gotoTime(QWidget *parent, double position, double duration)
{
    QDialog dialog(parent); dialog.setObjectName("gotoTimeDialog"); dialog.setWindowTitle(QStringLiteral("跳转时间"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("输入时:分:秒、分:秒或秒数，可带毫秒小数。")));
    auto *edit = new QLineEdit(timestamp(position)); edit->setObjectName("gotoTimeEdit");
    edit->setAccessibleName(QStringLiteral("目标时间")); layout->addWidget(edit);
    layout->addWidget(new QLabel(QStringLiteral("范围：%1 — %2").arg(timestamp(0), timestamp(duration))));
    auto *error = new QLabel; error->setObjectName("gotoTimeError"); error->setWordWrap(true); layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    std::optional<double> result;
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto time = parseTime(edit->text());
        if (!time || *time > duration) { error->setText(QStringLiteral("请输入有效时间，且不超过媒体总时长。")); edit->setFocus(); return; }
        result = time; dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(450, 180); edit->selectAll(); edit->setFocus(); dialog.exec(); return result;
}

void shortcuts(QWidget *parent, const PlayerActions &actions)
{
    QDialog dialog(parent); dialog.setObjectName("shortcutsDialog"); dialog.setWindowTitle(QStringLiteral("快捷键帮助"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *explanation = new QLabel(QStringLiteral("观看快捷键只在视频区生效。Tab 切换焦点；按钮、滑块、输入框、菜单和输入法保留自身按键。\nEsc 每次关闭一层；Ctrl+滚轮临时缩放，双击切换全屏。"));
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto *table = new QTableWidget(PlayerActions::Count, 4); table->setObjectName("shortcutsTable");
    table->setHorizontalHeaderLabels({QStringLiteral("操作"), QStringLiteral("快捷键"), QStringLiteral("作用域"), QStringLiteral("允许长按")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAccessibleName(QStringLiteral("当前快捷键")); table->verticalHeader()->hide();
    for (int i = 0; i < PlayerActions::Count; ++i) {
        auto *action = actions.get(PlayerActions::Id(i)); QStringList keys;
        for (const auto &key : action->shortcuts()) keys.append(key.toString(QKeySequence::NativeText));
        const QStringList columns{action->text(), keys.isEmpty() ? QStringLiteral("菜单入口") : keys.join(" / "),
            action->property("scope").toString(), action->autoRepeat() ? QStringLiteral("是") : QStringLiteral("否")};
        for (int column = 0; column < columns.size(); ++column) table->setItem(i, column, new QTableWidgetItem(columns[column]));
    }
    table->resizeColumnsToContents(); table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(780, 570); dialog.exec();
}
}
