// projectfile.h – a project is a folder: its confs, and one <folder name>.gpsproj that knows all of them and the
// templates they use with the revision each holds (TEMPLATE_PLAN D23, D25, like a .sln). The app writes it when it saves
// a conf, and keeps the path of every project in a hidden per-user list (ProjectRegistry), so it can tell which projects
// a template change reaches.
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include "graph.h"

class ProjectFile {
public:
    static QString pathForConf(const QString& conf);  // <folder>/<folder name>.gpsproj
    static ProjectFile load(const QString& path);     // empty when it does not exist yet

    QString path() const { return path_; }
    QStringList confs() const { return confs_.keys(); }  // file names in the folder
    QMap<QString, int> templates() const;                // every template any conf uses -> the lowest revision held
    // The conf was saved with this content: it is part of the project, using these templates.
    void record(const QString& conf, const gps::Graph& content);
    QString text() const;  // the INI text save writes
    bool save(QString* error = nullptr) const;

private:
    QString path_;
    QMap<QString, QMap<QString, int>> confs_;  // conf file name -> template name -> revision
};

namespace ProjectRegistry {
void remember(const QString& projectPath);
QStringList projects();                               // the ones still on disk
QStringList usingTemplate(const QString& templateName);  // project files a template is used in
}  // namespace ProjectRegistry
