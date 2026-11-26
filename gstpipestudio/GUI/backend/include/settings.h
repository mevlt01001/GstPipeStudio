// settings.h – the one owner of what the application remembers between starts (QSettings). Every key is spelled here
// and nowhere else; the rest of the application reads and writes typed values.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "layoutgraph.h"

namespace Settings {

// Where File > New project puts new projects; "" sets it back to ~/workspace/GstPipeStudio.
QString projectsLocation();
void setProjectsLocation(const QString& folder);

// File > Open recent: the last ten files opened or saved, newest first, and the folder the last one is in.
QStringList recentFiles();
QString lastDir();
void addRecent(const QString& path);

// The tabs open when the window last closed, by their targets' session keys, the backup of each (its id, "" when it
// had no unsaved changes) and the one in front.
struct Session {
    QStringList tabs;
    QStringList backups;
    int front = 0;
};
Session session();
void setSession(const Session& session);

QByteArray windowGeometry();
QByteArray windowState();
void setWindow(const QByteArray& geometry, const QByteArray& state);

layout::Routing linkRouting();
void setLinkRouting(layout::Routing routing);
bool autoAlign();
void setAutoAlign(bool on);

}  // namespace Settings
