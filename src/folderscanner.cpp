#include "folderscanner.h"
#include "playlistmodel.h"
#include <QCollator>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace { struct ScanResult { QStringList paths; int ignored = 0; }; }
void FolderScanner::cancel()
{
    if (!active_) return;
    active_->store(true); active_.reset(); emit busyChanged(false); emit canceled();
}
void FolderScanner::start(const QString &directory)
{
    cancel();
    auto token = std::make_shared<std::atomic_bool>(false); active_ = token; emit busyChanged(true);
    auto *watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, token, watcher] {
        if (active_ == token) { const auto result = watcher->result(); active_.reset(); emit busyChanged(false); emit completed(result.paths, result.ignored); }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([directory, token] {
        ScanResult result;
        QDirIterator iterator(directory, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::NoIteratorFlags);
        while (iterator.hasNext() && !token->load()) {
            const QString path = iterator.next();
            if (MediaFiles::media(path) && QFileInfo(path).isReadable()) result.paths.append(path); else ++result.ignored;
        }
        QCollator collator; collator.setNumericMode(true); collator.setCaseSensitivity(Qt::CaseInsensitive);
        if (!token->load()) std::stable_sort(result.paths.begin(), result.paths.end(), [&](const QString &left, const QString &right) {
            const int comparison = collator.compare(QFileInfo(left).fileName(), QFileInfo(right).fileName());
            return comparison ? comparison < 0 : left < right;
        });
        return result;
    }));
}
