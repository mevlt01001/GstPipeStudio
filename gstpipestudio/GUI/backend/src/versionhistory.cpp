// versionhistory.cpp – see versionhistory.h.
#include "versionhistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace VersionHistory {
namespace {
const char* kStamp = "yyyyMMdd-HHmmss-zzz";
}

QString folderFor(const QString& file) {
    const QFileInfo info(file);
    return QDir(info.absolutePath()).filePath(".gpsstudio/history/" + info.fileName());
}

bool keep(const QString& file, const QByteArray& replacement) {
    QFile current(file);
    if (!current.exists() || !current.open(QIODevice::ReadOnly)) return false;
    const QByteArray content = current.readAll();
    if (content == replacement) return false;
    QDir().mkpath(folderFor(file));
    QFile copy(QDir(folderFor(file)).filePath(QDateTime::currentDateTime().toString(kStamp) + ".conf"));
    if (!copy.open(QIODevice::WriteOnly) || copy.write(content) != content.size()) return false;
    copy.close();
    const QList<Version> all = versions(file);
    for (int i = kKept; i < all.size(); ++i) QFile::remove(all[i].path);
    return true;
}

QList<Version> versions(const QString& file) {
    QList<Version> out;
    const QDir dir(folderFor(file));
    for (const QString& name : dir.entryList({"*.conf"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QDateTime saved = QDateTime::fromString(QFileInfo(name).completeBaseName(), kStamp);
        if (saved.isValid()) out.append({saved, dir.filePath(name)});
    }
    return out;
}

}  // namespace VersionHistory
