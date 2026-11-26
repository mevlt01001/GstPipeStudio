// settings.cpp – see settings.h.
#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
constexpr int kRecentKept = 10;
}  // namespace

namespace Settings {

QString projectsLocation() {
    return QSettings().value("projects/location", QDir::home().filePath("workspace/GstPipeStudio")).toString();
}

void setProjectsLocation(const QString& folder) {
    if (folder.isEmpty()) QSettings().remove("projects/location");
    else QSettings().setValue("projects/location", folder);
}

QStringList recentFiles() { return QSettings().value("recentFiles").toStringList(); }

QString lastDir() { return QSettings().value("lastDir").toString(); }

void addRecent(const QString& path) {
    QStringList files = recentFiles();
    const QString file = QFileInfo(path).absoluteFilePath();
    files.removeAll(file);
    files.prepend(file);
    QSettings settings;
    settings.setValue("recentFiles", files.mid(0, kRecentKept));
    settings.setValue("lastDir", QFileInfo(path).absolutePath());
}

Session session() {
    QSettings settings;
    return {settings.value("session/tabs").toStringList(), settings.value("session/backups").toStringList(),
            settings.value("session/front").toInt()};
}

void setSession(const Session& session) {
    QSettings settings;
    settings.setValue("session/tabs", session.tabs);
    settings.setValue("session/backups", session.backups);
    settings.setValue("session/front", session.front);
}

QByteArray windowGeometry() { return QSettings().value("window/geometry").toByteArray(); }

QByteArray windowState() { return QSettings().value("window/state").toByteArray(); }

void setWindow(const QByteArray& geometry, const QByteArray& state) {
    QSettings settings;
    settings.setValue("window/geometry", geometry);
    settings.setValue("window/state", state);
}

layout::Routing linkRouting() {
    return QSettings().value("view/linkStyle").toString() == "rightAngles" ? layout::Routing::RightAngles : layout::Routing::Curves;
}

void setLinkRouting(layout::Routing routing) {
    QSettings().setValue("view/linkStyle", routing == layout::Routing::RightAngles ? "rightAngles" : "curves");
}

bool autoAlign() { return QSettings().value("view/autoAlign", false).toBool(); }

void setAutoAlign(bool on) { QSettings().setValue("view/autoAlign", on); }

}  // namespace Settings
