// savetarget.cpp – see savetarget.h.
#include "savetarget.h"

#include <QDir>
#include <QFileInfo>

#include "appservices.h"
#include "contract.h"
#include "projectfile.h"
#include "settings.h"
#include "templatelibrary.h"
#include "versionhistory.h"

QStringList SaveTarget::templateFolders() const { return {TemplateLibrary::directory()}; }

std::unique_ptr<SaveTarget> SaveTarget::fromKey(const QString& entry, AppServices& services) {
    const QString kind = kindOf(entry), value = valueOf(entry);
    if (kind == UntitledTarget::kKind && value.toInt() > 0) return std::make_unique<UntitledTarget>(value.toInt());
    if (kind == ProjectConfTarget::kKind && !value.isEmpty()) return std::make_unique<ProjectConfTarget>(value);
    if (auto* provider = services.provider(kind); provider && !value.isEmpty())
        return provider->documentTarget(value, ComponentProvider::DocumentAccess::Restore);
    return nullptr;
}

int UntitledTarget::lowestFree(const QList<const SaveTarget*>& open) {
    QList<int> taken;
    for (const SaveTarget* t : open) {
        if (auto* untitled = dynamic_cast<const UntitledTarget*>(t)) taken << untitled->number_;
    }
    int n = 1;
    while (taken.contains(n)) ++n;
    return n;
}

ProjectConfTarget::ProjectConfTarget(const QString& confPath) : path_(QFileInfo(confPath).absoluteFilePath()) {}

QString ProjectConfTarget::keyFor(const QString& confPath) { return key(kKind, QFileInfo(confPath).absoluteFilePath()); }

QString ProjectConfTarget::title() const { return QFileInfo(path_).fileName(); }

// Its own copies first: an earlier version is read from elsewhere, and still finds them.
QStringList ProjectConfTarget::templateFolders() const {
    return QStringList{ProjectFile::templatesFolder(path_)} + SaveTarget::templateFolders();
}

void ProjectConfTarget::opened() { ProjectFile::opened(path_); }

SaveResult ProjectConfTarget::write(const gps::Graph& plan) {
    gps::Graph saved = plan;
    saved.name = toStd(QFileInfo(path_).completeBaseName());
    const auto keepHistory = [this](const std::string& replacement, std::string* error) {
        QString reason;
        const bool kept = VersionHistory::keep(path_, QByteArray::fromStdString(replacement), &reason, templateFolders());
        if (!kept && error) *error = toStd(reason);
        return kept;
    };
    std::string failure;
    if (!saved.saveProject(toStd(path_), &failure, keepHistory)) return SaveResult::failed("Unable to save: " + fromStd(failure));
    ProjectFile project = ProjectFile::load(ProjectFile::pathForConf(path_));
    project.record(path_, saved);
    QString metadataError;
    if (!project.save(&metadataError) || !ProjectRegistry::remember(project.path(), &metadataError))
        return SaveResult::partial(path_, "Saved " + path_ + ". Unable to update project metadata: " + metadataError);
    Settings::addRecent(path_);
    return SaveResult::written(path_);
}

QString LibraryTemplateTarget::sessionKey() const { return key(library_.kind(), name_); }

QString LibraryTemplateTarget::path() const { return library_.file(name_); }

SaveResult LibraryTemplateTarget::write(const gps::Graph& plan) {
    gps::Graph next = plan;
    next.revision = library_.nextRevision(name_, plan.revision);
    return library_.save(name_, next);
}
