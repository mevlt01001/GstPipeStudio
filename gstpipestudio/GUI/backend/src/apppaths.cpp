// apppaths.cpp – see apppaths.h.
#include "apppaths.h"

#include <QDir>
#include <QStandardPaths>

namespace {

QString folder(QStandardPaths::StandardLocation location, const char* overrideName) {
    const QString root = qEnvironmentVariable("GPS_USER_DIR");
    const QString path = root.isEmpty() ? QStandardPaths::writableLocation(location) : QDir(root).filePath(overrideName);
    QDir().mkpath(path);
    return path;
}

QString below(const QString& base, const char* name) {
    const QString path = QDir(base).filePath(name);
    QDir().mkpath(path);
    return path;
}

}  // namespace

namespace AppPaths {

QString config() { return folder(QStandardPaths::AppConfigLocation, "config"); }
QString data() { return folder(QStandardPaths::AppDataLocation, "data"); }
QString state() { return folder(QStandardPaths::StateLocation, "state"); }
QString templates() { return below(data(), "templates"); }

}  // namespace AppPaths
