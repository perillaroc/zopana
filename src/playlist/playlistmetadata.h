#pragma once

#include <QSize>
#include <QString>
#include <optional>

struct PlaylistFileInfo {
    QString canonicalPath;
    qint64 bytes = -1;
    std::optional<qint64> modified, created; // Milliseconds since the epoch.

    bool versioned() const { return !canonicalPath.isEmpty() && bytes >= 0 && modified.has_value(); }
    bool operator==(const PlaylistFileInfo &) const = default;
};

struct PlaylistMetadata {
    enum class State { Unrequested, Read, Unknown };
    State state = State::Unrequested;
    QString title;
    std::optional<double> duration;
    QSize resolution;
};
