#include "playlistfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>

namespace {
bool hasDrive(QStringView path)
{
    if (path.size() < 2 || path[1] != u':') return false;
    const auto letter = path[0].toLower();
    return letter >= u'a' && letter <= u'z';
}

bool absoluteDrivePath(QStringView path)
{
    return hasDrive(path) && path.size() > 2 && path[2] == u'/';
}
}

namespace PlaylistFile {
Contents read(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return {.error = QStringLiteral("无法读取片单。%1").arg(file.errorString())};
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return {.error = QStringLiteral("无法读取片单。%1").arg(file.errorString())};
    QStringDecoder decode(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decode(bytes);
    if (decode.hasError()) return {.error = QStringLiteral("片单须使用 UTF-8 编码。")};
    auto content = QStringView(text);
    if (content.startsWith(QChar::ByteOrderMark)) content = content.sliced(1);

    Contents result;
    const QDir directory = QFileInfo(fileName).absoluteDir();
    for (const auto raw : content.split(u'\n')) {
        const auto line = raw.trimmed();
        if (line.startsWith(u"#EXT-X-", Qt::CaseInsensitive))
            return {.error = QStringLiteral("该文件是 HLS 片单，本地片单导入不支持 HLS。")};
        if (line.isEmpty() || line.startsWith(u'#')) continue;
        QString path = QDir::fromNativeSeparators(line.toString());
        if (!hasDrive(path)) {
            const QUrl url(path);
            if (!url.scheme().isEmpty()) {
                if (!url.isLocalFile() || !url.host().isEmpty()) {
                    ++result.networkPaths;
                    continue;
                }
                path = QDir::fromNativeSeparators(url.toLocalFile());
            }
        }
        // Normalize the Windows extended path spelling for drive-backed files.
        if (path.startsWith("//?/") && absoluteDrivePath(QStringView(path).sliced(4))) path.remove(0, 4);
        if (path.startsWith("//")) {
            ++result.networkPaths;
            continue;
        }
        if (path.isEmpty() || path.contains(QChar::Null) || path.startsWith(u':')
            || (hasDrive(path) && !absoluteDrivePath(path))) {
            ++result.invalidPaths;
            continue;
        }
        if (path.startsWith(u'/') && hasDrive(directory.path())) path.prepend(directory.path().left(2));
        const auto absolute = QDir::cleanPath(directory.absoluteFilePath(path));
        if (absolute.startsWith("//")) ++result.networkPaths;
        else result.paths.append(absolute);
    }
    return result;
}

QString write(const QString &fileName, const QStringList &paths)
{
    const QDir directory = QFileInfo(fileName).absoluteDir();
    QByteArray bytes("#EXTM3U\n");
    for (const auto &path : paths) {
        auto entry = directory.relativeFilePath(path);
        // A relative filename can begin with a comment character or whitespace.
        if (!entry.isEmpty() && (entry.startsWith(u'#') || entry.front().isSpace())) entry.prepend("./");
        bytes.append(entry.toUtf8());
        bytes.append('\n');
    }
    QSaveFile file(fileName);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return file.errorString();
    return {};
}
}
