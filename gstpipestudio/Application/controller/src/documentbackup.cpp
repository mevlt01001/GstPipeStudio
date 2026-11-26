// documentbackup.cpp – see documentbackup.h.
#include "documentbackup.h"

#include "controller.h"

DocumentBackup::DocumentBackup(BackupService& backups, Controller& controller, int delayMs)
    : backups_(backups), controller_(controller), id_(BackupService::newId()) {
    due_.setSingleShot(true);
    due_.setInterval(delayMs);
    connect(&due_, &QTimer::timeout, this, &DocumentBackup::write);
    connect(&controller_, &Controller::changed, this, &DocumentBackup::follow);
}

// It becomes this document's once the document holds the unsaved work: a backup that failed to open is left alone.
void DocumentBackup::continueFrom(const QString& id) {
    id_ = id;
    active_ = false;
    reopened_ = true;
    const std::optional<Backup> b = backups_.find(id);
    if (b && !b->base.isEmpty()) restored_ = b->base;
}

std::optional<Backup> DocumentBackup::held() const { return reopened_ && !active_ ? backups_.find(id_) : std::nullopt; }

void DocumentBackup::flush() {
    if (!due_.isActive()) return;
    due_.stop();
    write();
}

void DocumentBackup::discard() {
    due_.stop();
    if (active_) backups_.remove(id_);
    active_ = false;
}

// The first edit starts the timer, so a continuous burst still gets backed up at the configured interval.
void DocumentBackup::follow() {
    if (controller_.hasUnsavedWork()) {
        active_ = true;
        if (!due_.isActive()) due_.start();
    } else if (active_) {
        discard();
    }
}

void DocumentBackup::write() {
    if (!controller_.hasUnsavedWork()) return;
    const SaveTarget& target = controller_.target();
    backups_.write({id_, target.sessionKey(), target.title(), base_}, QByteArray::fromStdString(controller_.plan().toConf()));
}
