// templatelibrary.h – the user's saved multi-element presets ("templates"): one .conf per template in directory().
// One library serves every tab of the process and is the only reader and writer of that folder: it checks names,
// counts revisions, renames, deletes, imports and exports. Every write says changed(), and every tab reads it again.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <map>

#include "elementview.h"
#include "graph.h"
#include "saveresult.h"

class FactoryCatalog;

class TemplateLibrary : public QObject {
    Q_OBJECT
public:
    // Reads the folder now, and again when the catalog is ready (it tells which ends are sources and sinks).
    explicit TemplateLibrary(const FactoryCatalog& catalog, QObject* parent = nullptr);

    static QString directory();  // AppPaths::templates()
    QString file(const QString& name) const;  // the file the template is read from

    const QList<TemplateInfo>& list() const { return list_; }
    const gps::Graph* plan(const QString& name) const;  // nullptr when there is no such template
    gps::Graph content(const QString& name) const;      // a copy of it, empty when there is none
    int nextRevision(const QString& name, int base) const;  // the revision after both the library's and base
    QStringList projectsUsing(const QString& name) const;   // .gpsproj files of the projects it is used in

    // The name a template may take: trimmed, '#' made '_', not empty, and neither the name nor its file taken by another
    // template (except is the template's own name, when it is renamed). False with the reason in error.
    bool acceptName(const QString& wanted, QString* clean, QString* error, const QString& except = QString()) const;

    // The writes. Each says changed() when it wrote something.
    SaveResult save(const QString& name, gps::Graph content);  // a new template or its next revision
    bool rename(const QString& from, const QString& to, QString* clean, QString* error);
    bool remove(const QString& name);  // projects that use it keep their own copy in their templates folder
    bool exportTo(const QString& name, const QString& file, QString* error) const;
    QString importFrom(const QString& file, QString* error);  // the name it got, "" when refused
    void reload();  // reads the folder again

signals:
    void changed();

private:
    static QString fileFor(const QString& name);  // the file a template of this name is written to
    QString holder(const QString& name, const QString& except) const;  // the other template with this name or file, "" if none
    SaveResult write(const QString& name, gps::Graph content);  // the file and its history, no signal

    const FactoryCatalog& catalog_;
    QList<TemplateInfo> list_;
    std::map<QString, gps::Graph> plans_;
    std::map<QString, QString> files_;  // name -> the file it was read from; older versions named some differently
};
