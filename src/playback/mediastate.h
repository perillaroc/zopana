#pragma once
#include <QFileInfo>
#include <QVariant>
#include <QVector>
#include <mpv/client.h>

inline QVariant nodeValue(const mpv_node &node)
{
    switch (node.format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(node.u.string);
    case MPV_FORMAT_FLAG: return node.u.flag != 0;
    case MPV_FORMAT_INT64: return QVariant::fromValue<qlonglong>(node.u.int64);
    case MPV_FORMAT_DOUBLE: return node.u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList values; for (int i = 0; i < node.u.list->num; ++i) values.append(nodeValue(node.u.list->values[i])); return values;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap values; for (int i = 0; i < node.u.list->num; ++i) values[QString::fromUtf8(node.u.list->keys[i])] = nodeValue(node.u.list->values[i]); return values;
    }
    default: return {};
    }
}

inline QString mediaPathKey(const QString &path)
{
    const QFileInfo file(path); const auto canonical = file.canonicalFilePath();
    return (canonical.isEmpty() ? file.absoluteFilePath() : canonical).toCaseFolded();
}

struct MediaTrack
{
    qlonglong id = 0;
    int ordinal = 0;
    QString type, language, title, codec, externalPath;
    bool external = false, selected = false, image = false;
    QString fingerprint() const
    {
        if (external) return type + "|external|" + mediaPathKey(externalPath);
        return QStringLiteral("%1|%2|%3|%4|%5").arg(type, language, title, codec).arg(ordinal);
    }
    QString label() const
    {
        QStringList parts{QString::number(ordinal + 1)};
        if (!language.isEmpty()) parts << language;
        if (!title.isEmpty()) parts << title;
        if (!codec.isEmpty()) parts << codec;
        parts << (external ? QStringLiteral("外挂") : QStringLiteral("内封"));
        return parts.join(" · ");
    }
};

struct MediaChapter
{
    double start;
    QString title;
    bool operator==(const MediaChapter &) const = default;
};
