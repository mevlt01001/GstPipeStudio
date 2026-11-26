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

// The folder's sub-folders, then its files; a .conf opens in a tab.
QList<ExplorerEntry> listFolder(const QString& origin, const QString& dir, int depth) {
    QList<ExplorerEntry> out;
    for (const QFileInfo& f : QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name)) {
        if (f.isDir()) {
            ExplorerEntry folder = entry(Kind::Folder, origin, f.absoluteFilePath(), f.fileName());
            if (depth < kDepth) folder.children = listFolder(origin, f.absoluteFilePath(), depth + 1);
            out << folder;
        } else {
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
    const QString own = dir.filePath(dir.dirName() + ".conf");
    if (QFileInfo::exists(own)) return own;
    for (const QString& conf : ProjectFile::load(ProjectFile::pathForConf(own)).confs()) {
        if (QFileInfo::exists(dir.filePath(conf))) return dir.filePath(conf);
    }
    return QString();
}

ExplorerEntry componentEntry(const ComponentInfo& c) {
    ExplorerEntry e = entry(Kind::Component, c.ref.provider, c.ref.id, c.title);
    e.detail = c.description;
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
    if (e.state == State::Missing) return {ExplorerAction::CopyPath};
    return rowActions(e);
}

QList<ExplorerAction> ExplorerSource::rowActions(const ExplorerEntry& e) const {
    using A = ExplorerAction;
    switch (e.kind) {
    case Kind::Conf: return {A::Open, A::Rename, A::Delete, A::Reveal, A::CopyPath};
    case Kind::File: return {A::Rename, A::Delete, A::Reveal, A::CopyPath};
    case Kind::Folder: return {A::Delete, A::Reveal, A::CopyPath};
    case Kind::Project: return {A::Open, A::Delete, A::Reveal, A::CopyPath, A::NewProject};
    default: return {};
    }
}

QList<ExplorerSource*> ExplorerSource::standard(AppServices& services, QObject* parent) {
    QList<ExplorerSource*> out{new ProjectSection(services.library(), parent), new ProjectsSection(services.library(), parent)};
    for (ComponentProvider* provider : services.providers()) out << new ComponentSection(*provider, parent);
    return out;
}

// ---- ProjectSection

ProjectSection::ProjectSection(const TemplateLibrary& library, QObject* parent) : ExplorerSource(parent), library_(library) {}

QString ProjectSection::label(const EditorState& editor) const {
    const QString folder = folderOf(editor.projectConf);
    return folder.isEmpty() ? "Project" : "Project · " + QFileInfo(folder).fileName();
}

QString ProjectSection::tooltip(const EditorState& editor) const { return folderOf(editor.projectConf); }

QList<ExplorerEntry> ProjectSection::entries(const EditorState& editor) const {
    const QString folder = folderOf(editor.projectConf);
    if (folder.isEmpty()) return {};
    QList<ExplorerEntry> out = listFolder(id(), folder, 0);
    const ProjectFile manifest = ProjectFile::load(ProjectFile::pathForConf(editor.projectConf));
    for (ExplorerEntry& e : out) {
        if (e.kind == Kind::Conf) markOutdated(e, manifest.templatesOf(e.path), library_);
    }
    // Listed by the manifest or open in a tab, but not on disk.
    QStringList expected;
    for (const QString& conf : manifest.confs()) expected << QDir(folder).filePath(conf);
    for (const QString& open : editor.openFiles()) {
        if (folderOf(open) == folder && !expected.contains(open)) expected << open;
    }
    for (const QString& conf : std::as_const(expected)) {
        if (!QFileInfo::exists(conf)) out << entry(Kind::Conf, id(), conf, QFileInfo(conf).fileName(), State::Missing);
    }
    return out;
}

QStringList ProjectSection::watched(const EditorState& editor) const {
    const QString folder = folderOf(editor.projectConf);
    return folder.isEmpty() ? QStringList() : QStringList{folder, QDir(folder).filePath("templates")};
}

QList<ExplorerAction> ProjectSection::sectionActions() const { return {ExplorerAction::NewProject, ExplorerAction::Refresh}; }

// ---- ProjectsSection

ProjectsSection::ProjectsSection(const TemplateLibrary& library, QObject* parent) : ExplorerSource(parent), library_(library) {}

QString ProjectsSection::tooltip(const EditorState&) const { return Settings::projectsLocation(); }

QList<ExplorerEntry> ProjectsSection::entries(const EditorState&) const {
    QStringList folders;
    for (const QFileInfo& dir : QDir(Settings::projectsLocation()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) folders << dir.absoluteFilePath();
    for (const QString& manifest : ProjectRegistry::projects()) folders << QFileInfo(manifest).absolutePath();
    folders.removeDuplicates();
    QList<ExplorerEntry> out;
    for (const QString& folder : std::as_const(folders)) {
        const QString conf = mainConf(folder);
        if (conf.isEmpty()) continue;
        ExplorerEntry e = entry(Kind::Project, id(), conf, QFileInfo(folder).fileName());
        markOutdated(e, ProjectFile::load(ProjectFile::pathForConf(conf)).templates(), library_);
        out << e;
    }
    std::sort(out.begin(), out.end(), [](const ExplorerEntry& a, const ExplorerEntry& b) {
        const int byName = a.name.compare(b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.path < b.path;
    });
    return out;
}

QStringList ProjectsSection::watched(const EditorState&) const { return {Settings::projectsLocation()}; }

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
    return componentEntry({{provider.kind(), id}, id});
}

QList<ExplorerAction> ComponentSection::actionsOf(const ComponentProvider& provider, const QString& id) {
    QList<ExplorerAction> out;
    for (ComponentAction a : provider.actions(id)) {
        switch (a) {
        case ComponentAction::Edit: out << ExplorerAction::Edit; break;
        case ComponentAction::Rename: out << ExplorerAction::Rename; break;
        case ComponentAction::Export: out << ExplorerAction::Export; break;
        case ComponentAction::Delete: out << ExplorerAction::Delete; break;
        }
    }
    return out;
}

QList<ExplorerAction> ComponentSection::sectionActions() const {
    if (provider_.canImport()) return {ExplorerAction::Import, ExplorerAction::Refresh};
    return ExplorerSource::sectionActions();
}

QList<ExplorerAction> ComponentSection::rowActions(const ExplorerEntry& e) const { return actionsOf(provider_, e.path); }
