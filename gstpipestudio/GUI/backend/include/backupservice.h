// backupservice.h – hot exit: the unsaved content of the open tabs, kept in AppPaths::backups() so that quitting asks
// nothing and a crash loses about a second of work. A backup is two files: <id>.conf, the document as it was, and
// <id>.json, what it is the content of (its target's session key and title, and the file content it was based on). The next start reopens the tabs from
// the session and the backups (MainWindow::restoreSession). One per process, in AppServices; DocumentBackup writes
// one document's.
#pragma once

#include <QByteArray>
#include <QLockFile>
#include <QList>
#include <QString>
#include <memory>
#include <optional>

#include "settings.h"

struct Backup {
    QString id;     // "" for a tab to reopen that has no unsaved changes
    QString key;    // its target's session key: "file:…", "template:…", "untitled:N"
    QString title;  // its tab's text when it was written
    QByteArray base;  // the file content the unsaved work was based on, as DocumentRegistry hashes it; empty: none
    QString owner;    // the running application that writes it (BackupService's instance), "" for an older backup
};

// Two applications may run at once: each holds a lock in the folder while it runs, and leaves the backups another
// running one writes alone (it does not reopen, count or drop them). One whose application is gone is anyone's.
class BackupService {
public:
    BackupService();
    ~BackupService();

    // A backup the session does not name is counted at every start that offers it, and its tab writing it again starts
    // the count anew: one that no tab took up in this many starts cannot be loaded, and it goes. One the session names
    // is never counted: its tab comes back even when it could not load it (GStreamer not ready, say).
    static constexpr int kStartsKept = 3;

    static QString newId();
    static QString contentOf(const QString& id);  // the .conf of the backup, "" for no id

    bool write(const Backup& backup, const QByteArray& content) const;  // the content first, then what it is
    void remove(const QString& id) const;
    std::optional<Backup> find(const QString& id) const;  // a backup whose two files are there
    QList<Backup> list() const;                           // every backup in the folder, oldest first

    // The tabs to reopen: the session's, in order, each with its backup; a backup the session does not name (a crash
    // came before the window could save it) joins the session's tab of the same target, or comes after them. Every
    // backup the session does not name is counted first, and one offered kStartsKept times without being written again
    // is dropped.
    QList<Backup> reopen(const Settings::Session& session) const;

private:
    void count(const Settings::Session& session) const;  // one more start for every backup it does not name; the stale ones go
    QList<Backup> ours() const;  // list(), without the backups another running application writes
    bool foreign(const Backup& backup) const;  // another running application writes it
    void own() const;  // this application's lock in the backups folder now in use

    QString instance_;                        // this application's name in its backups and its lock
    mutable std::unique_ptr<QLockFile> lock_;
    mutable QString lockedIn_;                // the folder lock_ is in (tests move the folders)
};
