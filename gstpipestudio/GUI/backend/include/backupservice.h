// backupservice.h – hot exit: the unsaved content of the open tabs, kept in AppPaths::backups() so that quitting asks
// nothing and a crash loses about a second of work. A backup is two files: <id>.conf, the document as it was, and
// <id>.json, what it is the content of (its target's session key and title). The next start reopens the tabs from
// the session and the backups (MainWindow::restoreSession). One per process, in AppServices; DocumentBackup writes
// one document's.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <optional>

#include "settings.h"

struct Backup {
    QString id;     // "" for a tab to reopen that has no unsaved changes
    QString key;    // its target's session key: "file:…", "template:…", "untitled:N"
    QString title;  // its tab's text when it was written
};

class BackupService {
public:
    // A backup is counted at every start that offers it, and its tab writing it again starts the count anew: one that no
    // tab took up in this many starts cannot be loaded, and it goes.
    static constexpr int kStartsKept = 3;

    static QString newId();
    static QString contentOf(const QString& id);  // the .conf of the backup, "" for no id

    bool write(const Backup& backup, const QByteArray& content) const;  // the content first, then what it is
    void remove(const QString& id) const;
    std::optional<Backup> find(const QString& id) const;  // a backup whose two files are there
    QList<Backup> list() const;                           // every backup in the folder, oldest first

    // The tabs to reopen: the session's, in order, each with its backup; a backup the session does not name (a crash
    // came before the window could save it) joins the session's tab of the same target, or comes after them. Every
    // backup is counted first, and one offered kStartsKept times without being written again is dropped.
    QList<Backup> reopen(const Settings::Session& session) const;

private:
    void count() const;  // one more start for every backup; the stale ones go
};
