// documentregistry.cpp – see documentregistry.h.
#include "documentregistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include "controller.h"
#include "document.h"

DocumentRegistry::DocumentRegistry(QObject* parent) : QObject(parent) {
    settle_.setSingleShot(true);
    settle_.setInterval(kSettleMs);
    connect(&settle_, &QTimer::timeout, this, &DocumentRegistry::check);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, &settle_, qOverload<>(&QTimer::start));
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &settle_, qOverload<>(&QTimer::start));
}

// It is remembered when it reads or writes its file, and when it is bound to another one (a backup reopened on it).
void DocumentRegistry::track(Document* d) {
    Controller* c = d->controller();
    remember(d);
    connect(c, &Controller::synced, d, [this, d] { remember(d); });
    connect(c, &Controller::changed, d, [this, d] {
        if (entries_.value(d).path != d->path()) remember(d);
    });
    connect(d, &QObject::destroyed, this, [this, d] {
        entries_.remove(d);
        rewatch();
    });
}

bool DocumentRegistry::changedSinceRead(Document* d) const {
    const Entry e = entries_.value(d);
    const QByteArray now = stamp(e.path);
    return !now.isEmpty() && now != e.read;
}

void DocumentRegistry::accept(Document* d) {
    Entry& e = entries_[d];
    e.read = e.seen = stamp(e.path);
}

bool DocumentRegistry::catchUp(Document* d) {
    Entry& e = entries_[d];
    const QByteArray now = stamp(e.path);
    if (now.isEmpty() || now == e.seen) return false;
    e.seen = now;
    return now != e.read;
}

void DocumentRegistry::remember(Document* d) {
    Entry& e = entries_[d];
    const bool moved = e.path != d->path();
    e.path = d->path();
    e.read = e.seen = stamp(e.path);
    d->setFileDeleted(false);
    if (moved) rewatch();
}

void DocumentRegistry::check() {
    rewatch();  // a file replaced by a rename has left the watcher
    for (Document* d : entries_.keys()) {
        Entry& e = entries_[d];
        if (e.path.isEmpty()) continue;
        const QByteArray now = stamp(e.path);
        if (now.isEmpty()) {
            if (e.read.isEmpty() || d->fileDeleted()) continue;  // never written yet, or already said
            d->setFileDeleted(true);
            e.seen.clear();
            emit deletedOnDisk(d);
        } else if (now != e.seen || d->fileDeleted()) {
            const bool back = d->fileDeleted();
            const bool changed = now != e.read || back;  // a file back after a delete counts as changed
            d->setFileDeleted(false);
            if (back && now == e.read) d->controller()->restoreSavePoint();  // back as it was read
            e.seen = now;
            if (changed) emit changedOnDisk(d);
        }
    }
}

// The files and folders of the open documents that exist now; the rest leave the watcher.
void DocumentRegistry::rewatch() {
    QSet<QString> wanted;
    for (const Entry& e : std::as_const(entries_)) {
        if (!e.path.isEmpty()) wanted << e.path << QFileInfo(e.path).absolutePath();
    }
    const QStringList watched = watcher_.files() + watcher_.directories();
    QStringList gone, added;
    for (const QString& path : watched) {
        if (!wanted.contains(path)) gone << path;
    }
    for (const QString& path : std::as_const(wanted)) {
        if (!watched.contains(path) && QFileInfo::exists(path)) added << path;
    }
    if (!gone.isEmpty()) watcher_.removePaths(gone);
    if (!added.isEmpty()) watcher_.addPaths(added);
}

QByteArray DocumentRegistry::stamp(const QString& path) {
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) return QByteArray();
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1);
}
