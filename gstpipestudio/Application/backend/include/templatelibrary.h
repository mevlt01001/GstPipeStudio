// templatelibrary.h – the user's saved multi-element presets ("templates"): one .conf per template in the template directory.
// One library serves every tab of the process and is the only reader and writer of that folder: it checks names,
// counts revisions, renames, deletes, imports and exports. Every write says changed(), and every tab reads it again.
// It is the component provider of templates: the picker and the Explorer list it like any other provider.
#pragma once

#include <QList>
#include <QString>
#include <vector>

#include "componentprovider.h"
#include "graph.h"
#include "saveresult.h"

class FactoryCatalog;

class TemplateLibrary : public ComponentProvider {
    Q_OBJECT
public:
    // Reads the folder now; its components are described with what the catalog knows of their factories.
    explicit TemplateLibrary(FactoryCatalog& catalog, QObject* parent = nullptr);

    static ComponentRef ref(const QString& name) { return {kKind, name}; }
    QString file(const QString& name) const;

    // ---- as a component provider
    static constexpr const char* kKind = "template";
    QString kind() const override { return kKind; }
    QString title() const override { return "Templates"; }
    QString emptyHint() const override { return "Select elements and use Save as template"; }
    QList<ComponentInfo> components() const override;
    gps::Graph peek(const QString& name) const override { return content(name); }
    std::optional<gps::NodeRef> place(gps::Graph& g, const QString& name, double x, double y) const override;
    std::unique_ptr<SaveTarget> documentTarget(const QString& id, DocumentAccess access, QString* error = nullptr) override;
    QList<ComponentAction> actions(const QString& name) const override;
    bool canImport() const override { return true; }

    QStringList names() const;  // in the folder's order
    const gps::Graph* plan(const QString& name) const;  // nullptr when there is no such template
    gps::Graph content(const QString& name) const;      // a copy of it, empty when there is none
    int revision(const QString& name) const;                // the library's revision of it, 0 when there is none
    bool isNewer(const QString& name, int held) const { return revision(name) > held; }  // than a copy held at held
    int nextRevision(const QString& name, int base) const;  // the revision after both the library's and base
    QStringList projectsUsing(const QString& name) const;   // .gpsproj files of the projects it is used in

    // The name a template may take: trimmed, '#' made '_', not empty, and neither the name nor its file taken by another
    // template (except is the template's own name, when it is renamed). False with the reason in error.
    bool acceptName(const QString& wanted, QString* clean, QString* error, const QString& except = QString()) const;

    // The writes. Each says changed() when it wrote something.
    SaveResult save(const QString& name, gps::Graph content);  // a new template or its next revision
    QString nameError(const QString& name, const QString& wanted) const override;  // acceptName, as a component
    bool rename(const QString& from, const QString& to, QString* clean, QString* error) override;
    bool remove(const QString& name) override;  // to the trash; projects that use it keep their own copy in templates/
    QString removalNote(const QString& name) const override;
    bool exportTo(const QString& name, const QString& file, QString* error) const override;
    QString importFrom(const QString& file, QString* error) override;  // the name it got, "" when refused
    void reload() override;  // reads the folder again

private:
    QString holder(const QString& name, const QString& except) const;  // the other template with this name or file, "" if none
    SaveResult write(const QString& name, gps::Graph content, const QString& replacing);  // the file and its history, no signal
    void describe();  // asks the catalog for the factories its templates use that it has not described yet

    FactoryCatalog& catalog_;
    std::vector<gps::Graph> plans_;
};
