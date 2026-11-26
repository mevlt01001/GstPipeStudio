// documentbackup.cpp – see documentbackup.h.
#include "documentbackup.h"

#include "controller.h"

DocumentBackup::DocumentBackup(BackupService& backups, Controller* controller, QObject* parent)
    : QObject(parent), backups_(backups), controller_(controller), id_(BackupService::newId()) {
    due_.setSingleShot(true);
    due_.setInterval(kDelayMs);
    connect(&due_, &QTimer::timeout, this, &DocumentBackup::write);
    connect(controller, &Controller::changed, this, &DocumentBackup::follow);
}

// It becomes this document's once the document holds the unsaved work: a backup that failed to open is left alone.
void DocumentBackup::continueFrom(const QString& id) {
    id_ = id;
    active_ = false;
    reopened_ = true;
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

// The timer is not restarted by every change: a long burst of edits is still backed up about every second.
void DocumentBackup::follow() {
    if (controller_->hasUnsavedWork()) {
        active_ = true;
        if (!due_.isActive()) due_.start();
    } else if (active_) {
        discard();
    }
}

void DocumentBackup::write() {
    if (!controller_->hasUnsavedWork()) return;
    const SaveTarget& target = controller_->target();
    backups_.write({id_, target.sessionKey(), target.title()}, QByteArray::fromStdString(controller_->plan().toConf()));
}
