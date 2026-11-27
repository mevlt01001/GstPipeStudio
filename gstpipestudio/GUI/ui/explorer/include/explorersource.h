// explorersource.h – a section of the Explorer. Every section answers the same questions: its header, its rows, what
// it says while it is empty, the folders whose changes it shows and what its rows' menus offer. The Explorer is one
// tree of these, in order: Project (the front tab's folder), Projects, then one section per component provider
// (Templates, Plugins). A new kind of thing to show is a new section and nothing else.
#pragma once

#include <QList>
#include <QObject>
#include <QStringList>

#include "explorerentry.h"

class AppServices;
class ComponentProvider;
class TemplateLibrary;

// What a menu of the Explorer can do; FileOperations carries each one out.
enum class ExplorerAction {
    Open, Edit, Run, Duplicate, Rename, Delete, Export, Import, Reveal, CopyPath, NewPipeline, NewProject, Refresh,
    NewPlugin, AddComponent, EditPluginCode, EditPluginSpec, BuildPlugin, CopyAiPrompt, OpenPluginEditor, RevealPlugin
};

class ExplorerSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString id() const = 0;  // ExplorerEntry::origin of its rows
    virtual QString label(const EditorState& editor) const = 0;
    virtual QString tooltip(const EditorState&) const { return QString(); }
    virtual QList<ExplorerEntry> entries(const EditorState& editor) const = 0;
    virtual QString emptyHint() const = 0;
    virtual QStringList watched(const EditorState&) const { return {}; }  // folders whose changes it shows
    virtual QList<ExplorerAction> quickActions() const { return {}; }
    virtual void refresh() {}  // Refresh: reads again what it does not watch

    ExplorerEntry header(const EditorState& editor) const;  // its header row
    QList<ExplorerAction> actions(const ExplorerEntry& entry) const;  // the menu of a row or of its header

    // The Explorer's sections: Projects, and one per component provider.
    static QList<ExplorerSource*> standard(AppServices& services, QObject* parent);

signals:
    void changed();  // what it shows changed without a folder telling (a provider wrote)

protected:
    virtual QList<ExplorerAction> sectionActions() const { return {ExplorerAction::Refresh}; }
    virtual QList<ExplorerAction> rowActions(const ExplorerEntry& entry) const;  // by kind: what files offer
};

// Every project: the folders of the projects location, the projects the registry remembers, and any open project.
class ProjectsSection : public ExplorerSource {
    Q_OBJECT
public:
    ProjectsSection(const TemplateLibrary& library, QObject* parent);

    QString id() const override { return "projects"; }
    QString label(const EditorState&) const override { return "Projects"; }
    QString tooltip(const EditorState&) const override;
    QList<ExplorerEntry> entries(const EditorState& editor) const override;
    QString emptyHint() const override { return "No projects yet: use New project"; }
    QStringList watched(const EditorState&) const override;

protected:
    QList<ExplorerAction> sectionActions() const override;

private:
    const TemplateLibrary& library_;
};

// A component provider's components: the template library, the plugins.
class ComponentSection : public ExplorerSource {
    Q_OBJECT
public:
    ComponentSection(ComponentProvider& provider, QObject* parent);

    QString id() const override;
    QString label(const EditorState&) const override;
    QList<ExplorerEntry> entries(const EditorState& editor) const override;
    QString emptyHint() const override;
    void refresh() override;
    QList<ExplorerAction> quickActions() const override;

    // A component's row, and what its menu offers: what its provider supports.
    static ExplorerEntry entryOf(const ComponentProvider& provider, const QString& id);
    static QList<ExplorerAction> actionsOf(const ComponentProvider& provider, const QString& id);

protected:
    QList<ExplorerAction> sectionActions() const override;
    QList<ExplorerAction> rowActions(const ExplorerEntry& entry) const override;

private:
    ComponentProvider& provider_;
};
