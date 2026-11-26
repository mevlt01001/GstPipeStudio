// saveresult.h – what a write answers, whoever made it: written or not, where, and why not in the user's words.
#pragma once

#include <QString>

struct SaveResult {
    bool ok = false;
    QString path;      // the primary file written, including a partial save
    QString error;     // the failed operation and its reason
    int revision = 0;  // the revision written, when the destination counts them (a library template)

    static SaveResult written(const QString& path, int revision = 0) { return {true, path, QString(), revision}; }
    static SaveResult partial(const QString& path, const QString& why) { return {false, path, why, 0}; }
    static SaveResult failed(const QString& why) { return {false, QString(), why, 0}; }
};
