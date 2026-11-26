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
}  // namespace

QString BackupService::newId() { return QUuid::createUuid().toString(QUuid::Id128); }

QString BackupService::contentOf(const QString& id) { return id.isEmpty() ? QString() : fileOf(id, ".conf"); }

bool BackupService::write(const Backup& backup, const QByteArray& content) const {
    const QJsonObject about{{"key", backup.key}, {"title", backup.title}};
    return gps::files::writeAtomic(contentOf(backup.id).toStdString(), content.toStdString()) &&
           gps::files::writeAtomic(fileOf(backup.id, ".json").toStdString(), QJsonDocument(about).toJson().toStdString());
}

void BackupService::remove(const QString& id) const {
    if (id.isEmpty()) return;
    QFile::remove(fileOf(id, ".json"));
    QFile::remove(contentOf(id));
}

std::optional<Backup> BackupService::find(const QString& id) const {
    if (id.isEmpty() || !QFile::exists(contentOf(id))) return std::nullopt;
    QFile about(fileOf(id, ".json"));
    if (!about.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonObject json = QJsonDocument::fromJson(about.readAll()).object();
    if (json.value("key").toString().isEmpty()) return std::nullopt;
    return Backup{id, json.value("key").toString(), json.value("title").toString()};
}

QList<Backup> BackupService::list() const {
    QList<Backup> all;
    const QFileInfoList files = QDir(AppPaths::backups()).entryInfoList({"*.json"}, QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo& file : files) {
        if (const std::optional<Backup> b = find(file.completeBaseName())) all << *b;
    }
    return all;
}

QList<Backup> BackupService::reopen(const Settings::Session& session) const {
    QList<Backup> tabs;
    for (int i = 0; i < session.tabs.size(); ++i) {
        const std::optional<Backup> b = find(session.backups.value(i));
        tabs << (b ? *b : Backup{QString(), session.tabs[i], QString()});
    }
    for (const Backup& b : list()) {
        auto named = [&](const Backup& tab) { return tab.id == b.id; };
        auto sameTarget = [&](const Backup& tab) { return tab.id.isEmpty() && tab.key == b.key; };
        if (std::any_of(tabs.cbegin(), tabs.cend(), named)) continue;
        if (auto tab = std::find_if(tabs.begin(), tabs.end(), sameTarget); tab != tabs.end()) *tab = b;
        else tabs << b;
    }
    return tabs;
}
