// documentbackup.h – the hot-exit backup of one document. After the configured delay, unsaved work goes to the
// backups folder (BackupService); when the document is clean again (saved, reverted, undone
// to the save point) the backup goes. Quitting keeps it, and the next start reopens the document from it; closing the
// tab without saving drops it.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <optional>
#include <utility>

#include "backupservice.h"

class Controller;

class DocumentBackup : public QObject {
    Q_OBJECT
public:
    DocumentBackup(BackupService& backups, Controller& controller, int delayMs);

    QString id() const { return id_; }
    bool active() const { return active_; }   // it backs up unsaved work: once flushed, its backup is on disk
    void continueFrom(const QString& id);     // a document reopened from a backup goes on writing that one
    // The file content the document matches (DocumentRegistry's hash): written with the backup, so a reopened
    // document still knows what its unsaved work was based on, and a file changed since is said as changed.
    void setBase(const QByteArray& stamp) { base_ = stamp; }
    std::optional<QByteArray> takeRestoredBase() { return std::exchange(restored_, std::nullopt); }  // once, after continueFrom
    // The backup it was reopened from and has not taken up (its load failed): the session keeps naming it, so it is
    // offered again at the next start and never dropped.
    std::optional<Backup> held() const;
    void flush();                             // what is waiting is written now (the window quits)
    void discard();                           // its changes saved or dropped: its backup goes, a held one stays

private:
    void follow();  // the document changed
    void write();

    BackupService& backups_;
    Controller& controller_;
    QString id_;
    QTimer due_;
    bool active_ = false;    // the backup of id_ is this document's to write and to drop
    bool reopened_ = false;  // id_ is a backup it was reopened from
    QByteArray base_;
    std::optional<QByteArray> restored_;  // the base of the backup it was reopened from, until the registry takes it
};
