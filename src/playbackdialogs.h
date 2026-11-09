#pragma once

#include <QString>
#include <optional>

class QWidget;
class PlayerActions;

namespace PlaybackDialogs {
std::optional<double> parseTime(const QString &text);
std::optional<double> gotoTime(QWidget *parent, double position, double duration);
void shortcuts(QWidget *parent, const PlayerActions &actions);
QString timestamp(double seconds);
}
