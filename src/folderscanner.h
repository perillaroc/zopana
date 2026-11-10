#pragma once
#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>

class FolderScanner final : public QObject
{
    Q_OBJECT
public:
    explicit FolderScanner(QObject *parent = nullptr) : QObject(parent) {}
    ~FolderScanner() override { cancel(); }
    void start(const QString &directory);
    void cancel();
    bool busy() const { return bool(active_); }
signals:
    void busyChanged(bool busy);
    void completed(const QStringList &paths, int ignored);
    void canceled();
private:
    std::shared_ptr<std::atomic_bool> active_;
};
