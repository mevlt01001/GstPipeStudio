// versionhistory.h – earlier versions of a file, kept next to it in a hidden folder (TEMPLATE_PLAN D20, D25): before a
// save overwrites a file, the file as it was goes to <folder>/.gpsstudio/history/<file name>/<time>.conf. The newest 50
// are kept. Projects and library templates both use it.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace VersionHistory {

constexpr int kKept = 50;

struct Version {
    QDateTime saved;  // when it was replaced
    QString path;     // the kept copy
};

QString folderFor(const QString& file);
// Keeps the file as it is now, before replacement overwrites it; nothing happens when it does not exist yet or when
// replacement is what it already holds.
bool keep(const QString& file, const QByteArray& replacement);
QList<Version> versions(const QString& file);  // newest first

}  // namespace VersionHistory
