// templatelibrary.h – the user's saved multi-element presets ("templates"): one .conf per preset in directory().
#pragma once

#include <QList>
#include <QString>
#include <map>

#include "elementview.h"
#include "graph.h"

class FactoryCatalog;

class TemplateLibrary {
public:
    static QString directory();  // AppPaths::templates()

    void load(const FactoryCatalog& catalog);  // every preset; the catalog tells which ends are sources and sinks
    const QList<TemplateInfo>& list() const { return list_; }
    const gps::Graph* plan(const QString& name) const;  // nullptr when there is no such preset
    // Saves the preset with its top-left box at (0, 0), all of it one group; the file name comes from name.
    bool save(const QString& name, gps::Graph preset, QString* error);
    bool remove(const QString& name);

private:
    static QString fileFor(const QString& name);

    QList<TemplateInfo> list_;
    std::map<QString, gps::Graph> plans_;
};
