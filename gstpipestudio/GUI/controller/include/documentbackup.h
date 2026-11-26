// documentbackup.h – the hot-exit backup of one document. At most about a second after it changes while it has
// unsaved work, its content goes to the backups folder (BackupService); when it is clean again (saved, reverted, undone
// to the save point) the backup goes. Quitting keeps it, and the next start reopens the document from it; closing the
// tab without saving drops it.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>

#include "backupservice.h"

class Controller;

class DocumentBackup : public QObject {
    Q_OBJECT
public:
    static constexpr int kDelayMs = 1000;

    DocumentBackup(BackupService& backups, Controller* controller, QObject* parent);

    QString id() const { return id_; }
    bool active() const { return active_; }   // it backs up unsaved work: once flushed, its backup is on disk
    void continueFrom(const QString& id);     // a document reopened from a backup goes on writing that one
    // The backup it was reopened from and has not taken up (its load failed): the session keeps naming it, so it is
    // offered again at the next start and never dropped.
    std::optional<Backup> held() const;
    void flush();                             // what is waiting is written now (the window quits)
    void discard();                           // its changes saved or dropped: its backup goes, a held one stays

private:
    void follow();  // the document changed
    void write();

    BackupService& backups_;
    Controller* controller_;
    QString id_;
    QTimer due_;
    bool active_ = false;    // the backup of id_ is this document's to write and to drop
    bool reopened_ = false;  // id_ is a backup it was reopened from
};
