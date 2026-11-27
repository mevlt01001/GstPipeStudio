// explorersource.cpp – see explorersource.h.
#include "explorersource.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <algorithm>

#include "appservices.h"
#include "projectfile.h"
#include "settings.h"

using Kind = ExplorerEntry::Kind;
using State = ExplorerEntry::State;

namespace {

constexpr int kDepth = 3;  // deep trees stop after a few levels

ExplorerEntry entry(Kind kind, const QString& origin, const QString& path, const QString& name, State state = State::Ok) {
    ExplorerEntry e;
    e.kind = kind;
    e.origin = origin;
    e.path = path;
    e.name = name;
    e.state = state;
    return e;
}

// The folder's sub-folders, then its files; hidden files and internal .gpsproj manifests are omitted, a .conf opens in a tab.
QList<ExplorerEntry> listFolder(const QString& origin, const QString& dir, int depth) {
    QList<ExplorerEntry> out;
    for (const QFileInfo& f : QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name)) {
        if (f.fileName().startsWith('.')) continue;
        if (f.isDir()) {
            ExplorerEntry folder = entry(Kind::Folder, origin, f.absoluteFilePath(), f.fileName());
            if (depth < kDepth) folder.children = listFolder(origin, f.absoluteFilePath(), depth + 1);
            out << folder;
        } else {
            if (f.suffix() == "gpsproj") continue;
            out << entry(f.suffix() == "conf" ? Kind::Conf : Kind::File, origin, f.absoluteFilePath(), f.fileName());
        }
    }
    return out;
}

// The templates held at a revision older than the library's: an entry holding any is Outdated, and says which.
void markOutdated(ExplorerEntry& e, const QMap<QString, int>& held, const TemplateLibrary& library) {
    QStringList older;
    for (auto it = held.cbegin(); it != held.cend(); ++it) {
        if (library.isNewer(it.key(), it.value())) older << it.key();
    }
    if (older.isEmpty()) return;
    e.state = State::Outdated;
    e.detail = "Uses older revisions of: " + older.join(", ");
}

// A project's conf: <folder>/<folder name>.conf, or else the first conf its manifest lists that is there.
QString mainConf(const QString& folder) {
    const QDir dir(folder);
    const QString own = ProjectFile::mainConf(folder);
    if (QFileInfo::exists(own)) return own;
    for (const QString& conf : ProjectFile::load(ProjectFile::pathForConf(own)).confs()) {
        if (QFileInfo::exists(dir.filePath(conf))) return dir.filePath(conf);
    }
    return QString();
}

ExplorerEntry componentEntry(const ComponentInfo& c) {
    ExplorerEntry e = entry(Kind::Component, c.ref.provider, c.ref.id, c.title);
    e.detail = c.description;
    e.componentStatus = c.status;
    if (c.status != ComponentStatus::None) {
        const QStringList details{c.ref.id, c.description, c.hint};
        e.detail = details.join('\n');
    }
    return e;
}

QString folderOf(const QString& conf) { return conf.isEmpty() ? QString() : QFileInfo(conf).absolutePath(); }

}  // namespace

// ---- ExplorerSource

ExplorerEntry ExplorerSource::header(const EditorState& editor) const {
    ExplorerEntry e = entry(Kind::Section, id(), QString(), label(editor));
    e.detail = tooltip(editor);
    return e;
}

QList<ExplorerAction> ExplorerSource::actions(const ExplorerEntry& e) const {
    if (e.kind == Kind::Section) return sectionActions();
    if (e.kind == Kind::Hint) return {};
    // A conf not on disk: its tab, when one has it open (a new project before its first save, a file deleted under it),
    // can still come to the front.
    if (e.kind == Kind::Conf && !QFileInfo::exists(e.path))
        return e.state == State::Missing ? QList<ExplorerAction>{ExplorerAction::CopyPath} : QList<ExplorerAction>{ExplorerAction::Open, ExplorerAction::CopyPath};
    return rowActions(e);
}

QList<ExplorerAction> ExplorerSource::rowActions(const ExplorerEntry& e) const {
    using A = ExplorerAction;
    switch (e.kind) {
    case Kind::Conf: return {A::Open, A::Run, A::Duplicate, A::Rename, A::Delete, A::Reveal, A::CopyPath};
    case Kind::File: return {A::Rename, A::Delete, A::Reveal, A::CopyPath};
    case Kind::Folder:
        if (e.name != "templates") return {A::NewPipeline, A::Delete, A::Reveal, A::CopyPath};
        return {A::Delete, A::Reveal, A::CopyPath};
    case Kind::Project: return {A::Open, A::NewPipeline, A::Delete, A::Reveal, A::CopyPath, A::NewProject};
    default: return {};
    }
}

QList<ExplorerSource*> ExplorerSource::standard(AppServices& services, QObject* parent) {
    QList<ExplorerSource*> out{new ProjectsSection(services.library(), parent)};
    for (ComponentProvider* provider : services.providers()) out << new ComponentSection(*provider, parent);
    return out;
}

// ---- ProjectsSection

ProjectsSection::ProjectsSection(const TemplateLibrary& library, QObject* parent) : ExplorerSource(parent), library_(library) {}

QString ProjectsSection::tooltip(const EditorState&) const { return Settings::projectsLocation(); }

QList<ExplorerEntry> ProjectsSection::entries(const EditorState& editor) const {
    QStringList folders;
    for (const QFileInfo& dir : QDir(Settings::projectsLocation()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) folders << dir.absoluteFilePath();
    for (const QString& manifest : ProjectRegistry::projects()) folders << QFileInfo(manifest).absolutePath();
    if (const QString active = folderOf(editor.projectConf); !active.isEmpty()) folders << active;
    for (const QString& openFile : editor.openFiles()) {
        if (const QString f = folderOf(openFile); !f.isEmpty()) folders << f;
    }
    folders.removeDuplicates();
    QList<ExplorerEntry> out;
    for (const QString& folder : std::as_const(folders)) {
        QString conf = mainConf(folder);
        if (conf.isEmpty()) {
            for (const QString& openFile : editor.openFiles()) {
                if (folderOf(openFile) == folder) {
                    conf = openFile;
                    break;
                }
            }
        }
        if (conf.isEmpty()) continue;
        ExplorerEntry proj = entry(Kind::Project, id(), conf, QFileInfo(folder).fileName());
        const ProjectFile manifest = ProjectFile::load(ProjectFile::pathForConf(conf));
        if (QFileInfo(folder).isDir()) {
            proj.children = listFolder(id(), folder, 0);
            for (ExplorerEntry& child : proj.children) {
                if (child.kind == Kind::Conf) markOutdated(child, manifest.templatesOf(child.path), library_);
            }
        }
        QStringList expected;
        for (const QString& listed : manifest.confs()) expected << QDir(folder).filePath(listed);
        for (const QString& openFile : editor.openFiles()) {
            if (folderOf(openFile) == folder && !expected.contains(openFile)) expected << openFile;
        }
        for (const QString& missing : std::as_const(expected)) {
            if (!QFileInfo::exists(missing)) proj.children << entry(Kind::Conf, id(), missing, QFileInfo(missing).fileName(), State::Missing);
        }
        markOutdated(proj, manifest.templates(), library_);
        out << proj;
    }
    std::sort(out.begin(), out.end(), [](const ExplorerEntry& a, const ExplorerEntry& b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.path < b.path;
    });
    return out;
}

QStringList ProjectsSection::watched(const EditorState& editor) const {
    QStringList dirs{Settings::projectsLocation()};
    if (const QString active = folderOf(editor.projectConf); !active.isEmpty()) {
        dirs << active << ProjectFile::templatesFolder(editor.projectConf);
    }
    for (const QString& openFile : editor.openFiles()) {
        if (const QString f = folderOf(openFile); !f.isEmpty()) dirs << f;
    }
    dirs.removeDuplicates();
    return dirs;
}

QList<ExplorerAction> ProjectsSection::sectionActions() const { return {ExplorerAction::NewProject, ExplorerAction::Refresh}; }

// ---- ComponentSection

ComponentSection::ComponentSection(ComponentProvider& provider, QObject* parent) : ExplorerSource(parent), provider_(provider) {
    connect(&provider, &ComponentProvider::changed, this, &ExplorerSource::changed);
}

QString ComponentSection::id() const { return provider_.kind(); }

QString ComponentSection::label(const EditorState&) const { return provider_.title(); }

QString ComponentSection::emptyHint() const { return provider_.emptyHint(); }

void ComponentSection::refresh() { provider_.reload(); }

QList<ExplorerEntry> ComponentSection::entries(const EditorState&) const {
    QList<ExplorerEntry> out;
    for (const ComponentInfo& c : provider_.components()) out << componentEntry(c);
    return out;
}

ExplorerEntry ComponentSection::entryOf(const ComponentProvider& provider, const QString& id) {
    for (const ComponentInfo& c : provider.components()) {
        if (c.ref.id == id) return componentEntry(c);
    }
    ComponentInfo fallback;
    fallback.ref = {provider.kind(), id};
    fallback.title = id;
    return componentEntry(fallback);
}

QList<ExplorerAction> ComponentSection::actionsOf(const ComponentProvider& provider, const QString& id) {
    QList<ExplorerAction> out;
    for (ComponentAction a : provider.actions(id)) {
        switch (a) {
        case ComponentAction::Edit: out << ExplorerAction::Edit; break;
        case ComponentAction::Rename: out << ExplorerAction::Rename; break;
        case ComponentAction::Export: out << ExplorerAction::Export; break;
        case ComponentAction::Delete: out << ExplorerAction::Delete; break;
        case ComponentAction::Place: out << ExplorerAction::AddComponent; break;
        case ComponentAction::EditCode: out << ExplorerAction::EditPluginCode; break;
        case ComponentAction::EditSpec: out << ExplorerAction::EditPluginSpec; break;
        case ComponentAction::Build: out << ExplorerAction::BuildPlugin; break;
        case ComponentAction::CopyAiPrompt: out << ExplorerAction::CopyAiPrompt; break;
        case ComponentAction::ExternalEditor: out << ExplorerAction::OpenPluginEditor; break;
        case ComponentAction::Reveal: out << ExplorerAction::RevealPlugin; break;
        }
    }
    return out;
}

QList<ExplorerAction> ComponentSection::sectionActions() const {
    QList<ExplorerAction> actions;
    if (provider_.canCreate()) actions << ExplorerAction::NewPlugin;
    if (provider_.canImport()) actions << ExplorerAction::Import;
    return actions << ExplorerAction::Refresh;
}

QList<ExplorerAction> ComponentSection::quickActions() const {
    if (provider_.canCreate()) return {ExplorerAction::NewPlugin, ExplorerAction::Refresh};
    return {};
}

QList<ExplorerAction> ComponentSection::rowActions(const ExplorerEntry& e) const { return actionsOf(provider_, e.path); }
