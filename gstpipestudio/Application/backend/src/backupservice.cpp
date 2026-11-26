// backupservice.cpp – see backupservice.h.
#include "backupservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>

#include "apppaths.h"
#include "files.h"

namespace {
QString fileOf(const QString& id, const char* suffix) { return QDir(AppPaths::backups()).filePath(id + suffix); }

QJsonObject aboutOf(const QString& id) {
    QFile about(fileOf(id, ".json"));
    return about.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(about.readAll()).object() : QJsonObject();
}

bool writeAbout(const QString& id, const QJsonObject& about) {
    return gps::files::writeAtomic(fileOf(id, ".json").toStdString(), QJsonDocument(about).toJson().toStdString());
}
}  // namespace

BackupService::BackupService() : instance_(newId()) {}

BackupService::~BackupService() = default;

QString BackupService::newId() { return QUuid::createUuid().toString(QUuid::Id128); }

QString BackupService::contentOf(const QString& id) { return id.isEmpty() ? QString() : fileOf(id, ".conf"); }

bool BackupService::write(const Backup& backup, const QByteArray& content) const {
    own();
    return gps::files::writeAtomic(contentOf(backup.id).toStdString(), content.toStdString()) &&
           writeAbout(backup.id, {{"key", backup.key}, {"title", backup.title}, {"base", QString::fromLatin1(backup.base.toHex())},
                                  {"owner", instance_}});
}

void BackupService::remove(const QString& id) const {
    if (id.isEmpty()) return;
    QFile::remove(fileOf(id, ".json"));
    QFile::remove(contentOf(id));
}

std::optional<Backup> BackupService::find(const QString& id) const {
    if (id.isEmpty() || !QFile::exists(contentOf(id))) return std::nullopt;
    const QJsonObject json = aboutOf(id);
    if (json.value("key").toString().isEmpty()) return std::nullopt;
    return Backup{id, json.value("key").toString(), json.value("title").toString(),
                  QByteArray::fromHex(json.value("base").toString().toLatin1()), json.value("owner").toString()};
}

QList<Backup> BackupService::list() const {
    QList<Backup> all;
    // In the order their content was written: counting the starts rewrites only what a backup is of.
    const QFileInfoList files = QDir(AppPaths::backups()).entryInfoList({"*.conf"}, QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo& file : files) {
        if (const std::optional<Backup> b = find(file.completeBaseName())) all << *b;
    }
    return all;
}

QList<Backup> BackupService::ours() const {
    QList<Backup> out;
    for (const Backup& b : list()) {
        if (!foreign(b)) out << b;
    }
    return out;
}

// Its owner's lock is held: that application still runs. A lock left by one that is gone is stale, and tryLock takes it.
bool BackupService::foreign(const Backup& b) const {
    if (b.owner.isEmpty() || b.owner == instance_) return false;
    QLockFile other(fileOf(b.owner, ".lock"));
    other.setStaleLockTime(0);  // a running application's lock is never too old
    if (other.tryLock(0)) {
        other.unlock();
        return false;
    }
    return other.error() == QLockFile::LockFailedError;
}

void BackupService::own() const {
    const QString folder = AppPaths::backups();
    if (lock_ && lockedIn_ == folder) return;
    QDir().mkpath(folder);
    lock_ = std::make_unique<QLockFile>(fileOf(instance_, ".lock"));
    lock_->tryLock(0);
    lockedIn_ = folder;
}

void BackupService::count(const Settings::Session& session) const {
    for (const Backup& b : ours()) {
        if (session.backups.contains(b.id)) continue;  // its tab comes back, loaded or not
        QJsonObject about = aboutOf(b.id);
        const int starts = about.value("starts").toInt() + 1;
        if (starts > kStartsKept) {
            remove(b.id);
            continue;
        }
        about["starts"] = starts;
        writeAbout(b.id, about);
    }
}

QList<Backup> BackupService::reopen(const Settings::Session& session) const {
    count(session);
    QList<Backup> tabs;
    for (int i = 0; i < session.tabs.size(); ++i) {
        const std::optional<Backup> b = find(session.backups.value(i));
        tabs << (b && !foreign(*b) ? *b : Backup{QString(), session.tabs[i], QString()});
    }
    for (const Backup& b : ours()) {
        auto named = [&](const Backup& tab) { return tab.id == b.id; };
        auto sameTarget = [&](const Backup& tab) { return tab.id.isEmpty() && tab.key == b.key; };
        if (std::any_of(tabs.cbegin(), tabs.cend(), named)) continue;
        if (auto tab = std::find_if(tabs.begin(), tabs.end(), sameTarget); tab != tabs.end()) *tab = b;
        else tabs << b;
    }
    return tabs;
}
