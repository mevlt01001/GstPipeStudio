// fileoperations.cpp – see fileoperations.h.
#include "fileoperations.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QUrl>

#include "appservices.h"
#include "controller.h"
#include "document.h"
#include "documenttabs.h"
#include "explorerlook.h"
#include "mainwindow.h"
#include "newprojectdialog.h"
#include "projectfile.h"
#include "prompt.h"
#include "savetarget.h"
#include "versionhistory.h"

using Kind = ExplorerEntry::Kind;

namespace {

// The manifest of a conf's project, changed by edit and written when edit changed it (the conf is listed).
void editManifest(const QString& conf, const std::function<bool(ProjectFile&)>& edit) {
    ProjectFile manifest = ProjectFile::load(ProjectFile::pathForConf(conf));
    if (edit(manifest)) manifest.save();
}

}  // namespace

FileOperations::FileOperations(AppServices& services, MainWindow* window) : QObject(window), services_(services), window_(window) {
    // Renamed anywhere (here, the picker, a test), a component's tab follows: it saves under the new name.
    for (ComponentProvider* provider : services.providers()) {
        connect(provider, &ComponentProvider::renamed, this, [this, provider](const QString& from, const QString& to) {
            const QString key = ComponentSection::entryOf(*provider, to).key();
            rebind(ComponentSection::entryOf(*provider, from).key(), SaveTarget::fromKey(key, services_.library()));
        });
    }
}

void FileOperations::run(ExplorerAction action, const ExplorerEntry& entry) {
    switch (action) {
    case ExplorerAction::Open:
    case ExplorerAction::Edit: open(entry); break;
    case ExplorerAction::Rename:
        if (const auto to = Prompt::text(window_, "Rename", "New name of " + entry.name, entry.name)) rename(entry, *to);
        break;
    case ExplorerAction::Delete: remove(entry); break;
    case ExplorerAction::Export: exportComponent(entry); break;
    case ExplorerAction::Import: importInto(entry.origin); break;
    case ExplorerAction::Reveal: reveal(entry); break;
    case ExplorerAction::CopyPath: copyPath(entry); break;
    case ExplorerAction::NewProject: newProject(); break;
    case ExplorerAction::Refresh: break;  // the Explorer's own: it reads its sections again
    }
}

void FileOperations::popup(const QList<ExplorerAction>& actions, const ExplorerEntry& entry, const QPoint& at, const Own& own) {
    if (actions.isEmpty()) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("explorerMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (ExplorerAction action : actions) {
        if (action == ExplorerAction::Delete || action == ExplorerAction::NewProject || action == ExplorerAction::Refresh) menu->addSeparator();
        menu->addAction(ExplorerLook::actionText(action, entry), this, [this, action, entry, own] {
            if (!own || !own(action)) run(action, entry);
        })->setObjectName(ExplorerLook::actionName(action));
    }
    menu->popup(at);
}

void FileOperations::componentMenu(const ComponentRef& component, const QPoint& at) {
    if (const ComponentProvider* provider = services_.provider(component.provider))
        popup(ComponentSection::actionsOf(*provider, component.id), ComponentSection::entryOf(*provider, component.id), at);
}

void FileOperations::open(const ExplorerEntry& entry) {
    if (entry.kind == Kind::Conf || entry.kind == Kind::Project) window_->openFile(entry.path);
    else if (entry.kind == Kind::Component) window_->openComponent({entry.origin, entry.path});
}

QString FileOperations::nameError(const ExplorerEntry& entry, const QString& wanted) const {
    if (entry.kind == Kind::Component) {
        const ComponentProvider* provider = services_.provider(entry.origin);
        return provider ? provider->nameError(entry.path, wanted) : "There is no such component.";
    }
    const QString name = wanted.trimmed();
    if (name.isEmpty()) return "Give it a name.";
    if (name.contains('/') || name == "." || name == "..") return "A name cannot contain / or be . or ..";
    if (entry.kind == Kind::Conf && !name.endsWith(".conf")) return "A pipeline file keeps its .conf ending.";
    if (name != entry.name && QFileInfo::exists(QFileInfo(entry.path).dir().filePath(name))) return name + " already exists here.";
    return QString();
}

bool FileOperations::rename(const ExplorerEntry& entry, const QString& wanted) {
    if (const QString why = nameError(entry, wanted); !why.isEmpty()) {
        fail("Rename", why);
        return false;
    }
    QString renamed, error;
    ComponentProvider* provider = entry.kind == Kind::Component ? services_.provider(entry.origin) : nullptr;
    const bool ok = provider ? provider->rename(entry.path, wanted, &renamed, &error) : renameFile(entry, wanted.trimmed());
    if (!ok && provider) fail("Rename", error);
    if (ok) emit wrote();
    return ok;
}

// Its version history goes with it; a conf's manifest entry follows, and its tab saves to the new file.
bool FileOperations::renameFile(const ExplorerEntry& entry, const QString& name) {
    const QString to = QFileInfo(entry.path).dir().filePath(name);
    if (to == entry.path) return true;
    if (!QFile::rename(entry.path, to)) {
        fail("Rename", "Unable to rename " + entry.name + " to " + name + ".");
        return false;
    }
    QDir().rename(VersionHistory::folderFor(entry.path), VersionHistory::folderFor(to));
    if (entry.kind != Kind::Conf) return true;
    editManifest(entry.path, [&](ProjectFile& manifest) { return manifest.rename(entry.path, to); });
    rebind(entry.key(), std::make_unique<ProjectConfTarget>(to));
    return true;
}

bool FileOperations::remove(const ExplorerEntry& entry) {
    QStringList unsaved;
    for (const Document* d : tabsOf(entry)) {
        if (d->controller()->hasUnsavedWork()) unsaved << d->title();
    }
    ComponentProvider* provider = entry.kind == Kind::Component ? services_.provider(entry.origin) : nullptr;
    QStringList notes;
    if (provider) notes << provider->removalNote(entry.path);
    if (!unsaved.isEmpty()) notes << "Open with unsaved changes: " + unsaved.join(", ") + ". The tab keeps them, marked deleted.";
    notes.removeAll(QString());
    const QString question = provider ? "Delete the " + entry.origin + " " + entry.name + " from the library? It goes to the trash."
                             : entry.isFolder() ? "Move " + entry.name + " and everything in it to the trash?"
                                                : "Move " + entry.name + " to the trash?";
    const Prompt::Question ask{"confirmDelete", "Delete", question, notes.join("\n\n"),
                               {{"Move to trash", "moveToTrash", QMessageBox::DestructiveRole}, Prompt::cancel()}, 1,
                               unsaved.isEmpty() ? QMessageBox::Question : QMessageBox::Warning};
    if (Prompt::ask(window_, ask) != 0) return false;

    const bool removed = provider ? provider->remove(entry.path) : QFile::moveToTrash(entry.location());
    if (!removed) {
        fail("Delete", "Unable to move " + entry.name + " to the trash.");
        return false;
    }
    if (entry.kind == Kind::Conf)  // it leaves its project
        editManifest(entry.path, [&](ProjectFile& manifest) { return manifest.forget(entry.path); });
    emit wrote();
    return true;
}

// A file shows its folder: the file managers do not agree on how to point at one file.
void FileOperations::reveal(const ExplorerEntry& entry) {
    opener_->open(QUrl::fromLocalFile(entry.isFolder() ? entry.location() : QFileInfo(entry.location()).absolutePath()));
}

void FileOperations::copyPath(const ExplorerEntry& entry) {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->setText(entry.location());
}

void FileOperations::exportComponent(const ExplorerEntry& entry) {
    const ComponentProvider* provider = services_.provider(entry.origin);
    if (!provider) return;
    const QString file = QFileDialog::getSaveFileName(window_, "Export " + entry.origin, QDir::home().filePath(entry.path + ".conf"), "Templates (*.conf)");
    QString error;
    if (!file.isEmpty() && !provider->exportTo(entry.path, file, &error)) fail("Export " + entry.origin, error);
}

void FileOperations::importInto(const QString& providerKind) {
    ComponentProvider* provider = services_.provider(providerKind);
    if (!provider || !provider->canImport()) return;
    const QString file = QFileDialog::getOpenFileName(window_, "Import " + providerKind, QDir::homePath(), "Templates (*.conf)");
    if (file.isEmpty()) return;
    QString error;
    if (provider->importFrom(file, &error).isEmpty()) fail("Import " + providerKind, error);
    else emit wrote();
}

// Asks its name; nothing is written yet: the first save makes the folder.
void FileOperations::newProject() {
    const QString conf = NewProjectDialog::ask(window_, window_->documentTabs()->openFiles());
    if (!conf.isEmpty()) window_->documentTabs()->target()->controller()->newPlan(std::make_unique<ProjectConfTarget>(conf));
}

QList<Document*> FileOperations::tabsOf(const ExplorerEntry& entry) const {
    QList<Document*> out;
    const QString where = entry.location();
    for (Document* d : window_->documents()) {
        const QString path = d->path();
        const bool inside = !where.isEmpty() && !path.isEmpty() && (path == where || path.startsWith(where + "/"));
        if (inside || (entry.kind == Kind::Component && d->shows(entry.key()))) out << d;
    }
    return out;
}

void FileOperations::rebind(const QString& key, std::unique_ptr<SaveTarget> target) {
    if (!target) return;
    for (Document* d : window_->documents()) {
        if (d->shows(key)) {
            d->controller()->setTarget(std::move(target));
            return;
        }
    }
}

void FileOperations::fail(const QString& title, const QString& text) const { Prompt::error(window_, title, text); }
