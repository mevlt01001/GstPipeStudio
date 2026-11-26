// versionhistory.cpp – see versionhistory.h.
#include "versionhistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "contract.h"
#include "files.h"
#include "graph.h"

namespace VersionHistory {
namespace {
const char* kStamp = "yyyyMMdd-HHmmss-zzz";
}

QString folderFor(const QString& file) {
    const QFileInfo info(file);
    return QDir(info.absolutePath()).filePath(".gpsstudio/history/" + info.fileName());
}

bool keep(const QString& file, const QByteArray& replacement, QString* error, const QStringList& templateFolders) {
    QFile current(file);
    if (!current.exists()) return true;
    if (!current.open(QIODevice::ReadOnly)) {
        if (error) *error = "Cannot read the previous version of " + file + ": " + current.errorString();
        return false;
    }
    QByteArray content = current.readAll();
    if (current.error() != QFileDevice::NoError) {
        if (error) *error = "Cannot read the previous version of " + file + ": " + current.errorString();
        return false;
    }
    if (content == replacement) return true;
    if (!templateFolders.isEmpty()) {
        gps::Graph previous;
        if (gps::Graph::load(toStd(file), &previous, nullptr, toStd(templateFolders)))
            content = QByteArray::fromStdString(previous.toConf());
    }
    const QDir folder(folderFor(file));
    auto time = QDateTime::currentDateTime();
    std::string failure;
    for (;;) {
        const QString copy = folder.filePath(time.toString(kStamp) + ".conf");
        const auto result = gps::files::createAtomic(toStd(copy), content.toStdString(), &failure);
        if (result == gps::files::CreateResult::Created) break;
        if (result == gps::files::CreateResult::Failed) {
            if (error) *error = fromStd(failure);
            return false;
        }
        time = time.addMSecs(1);
    }
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
