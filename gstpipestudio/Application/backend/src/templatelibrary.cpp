// templatelibrary.cpp – see templatelibrary.h.
#include "templatelibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <utility>

#include "apppaths.h"
#include "contract.h"
#include "factorycatalog.h"
#include "projectfile.h"
#include "savetarget.h"
#include "templates.h"
#include "versionhistory.h"

TemplateLibrary::TemplateLibrary(FactoryCatalog& catalog, QObject* parent) : ComponentProvider(parent), catalog_(catalog) {
    connect(&catalog, &FactoryCatalog::ready, this, &TemplateLibrary::reload);
    reload();
}

// Use the same canonical name for discovery, writes and project references.
QString TemplateLibrary::file(const QString& name) const {
    return QDir(AppPaths::templates()).filePath(fromStd(gps::templates::fileName(toStd(name))));
}

void TemplateLibrary::reload() {
    plans_.clear();
    const QDir folder(AppPaths::templates());
    for (const QString& name : folder.entryList({"*.conf"}, QDir::Files, QDir::Name)) {
        const QString path = folder.filePath(name);
        gps::Graph plan;
        if (!gps::Graph::load(toStd(path), &plan, nullptr) || plan.elements.empty() ||
            file(fromStd(plan.name)) != path) continue;
        plans_.push_back(std::move(plan));
    }
    describe();
    emit changed();
}

QStringList TemplateLibrary::names() const {
    QStringList result;
    for (const gps::Graph& plan : plans_) result << fromStd(plan.name);
    return result;
}

// A template's ports and role come from its factories' definitions, which the catalog has only for the factories some
// tab asked about. Once the catalog has its list, the library asks for the rest; a factory not installed is left out.
void TemplateLibrary::describe() {
    if (!catalog_.loaded()) return;
    QStringList missing;
    for (const gps::Graph& content : plans_) {
        for (const QString& factory : catalog_.missing(content)) {
            if (catalog_.entry(factory) && !missing.contains(factory)) missing << factory;
        }
    }
    catalog_.fetchInfos(missing, this, [](bool) {});
}

// A template is described as one node: its pads are those its members leave free. A factory the catalog has not
// described yet gives no pads, so such a template may still take an input.
QList<ComponentInfo> TemplateLibrary::components() const {
    QList<ComponentInfo> out;
    for (const gps::Graph& content : plans_) {
        const QString name = fromStd(content.name);
        QStringList chain;  // the member factories, one per element
        for (const gps::Element& e : content.elements) chain << fromStd(e.factory);
        const bool known = catalog_.missing(content).isEmpty();
        const gps::NodeShape shape(content, [this](const std::string& factory) { return catalog_.definition(fromStd(factory)); });
        const gps::NodePorts ports = shape.ports(content.elementNames());
        ComponentInfo info;
        info.ref = ref(name);
        info.title = name;
        info.description = chain.join(" → ");
        info.hint = QString("template · %1").arg(content.elements.size());
        info.takesInput = !known || !ports.free(gps::Pad::Direction::Sink).empty() || !ports.requests.empty();
        info.givesOutput = !ports.free(gps::Pad::Direction::Source).empty();
        info.role = gps::NodeShape::role(ports);
        out << info;
    }
    return out;
}

std::optional<gps::NodeRef> TemplateLibrary::place(gps::Graph& g, const QString& name, double x, double y) const {
    const gps::Graph* tmpl = plan(name);
    if (!tmpl) return std::nullopt;
    const auto [left, top] = tmpl->topLeft();
    return gps::NodeRef::instance(gps::templates::insertTemplate(g, *tmpl, x - left, y - top));
}

std::unique_ptr<SaveTarget> TemplateLibrary::documentTarget(const QString& name, DocumentAccess access, QString* error) {
    if (name.isEmpty() || (access == DocumentAccess::Open && !plan(name))) {
        if (error) *error = "Template " + name + " is not in the library";
        return nullptr;
    }
    return std::make_unique<LibraryTemplateTarget>(name, *this);
}

QList<ComponentAction> TemplateLibrary::actions(const QString& name) const {
    if (!plan(name)) return {};
    return {ComponentAction::Edit, ComponentAction::Rename, ComponentAction::Export, ComponentAction::Delete};
}

const gps::Graph* TemplateLibrary::plan(const QString& name) const {
    const std::string key = toStd(name);
    const auto found = std::find_if(plans_.begin(), plans_.end(), [&](const gps::Graph& plan) { return plan.name == key; });
    return found == plans_.end() ? nullptr : &*found;
}

int TemplateLibrary::revision(const QString& name) const {
    const gps::Graph* p = plan(name);
    return p ? p->revision : 0;
}

gps::Graph TemplateLibrary::content(const QString& name) const {
    const gps::Graph* found = plan(name);
    return found ? *found : gps::Graph();
}

int TemplateLibrary::nextRevision(const QString& name, int base) const {
    return std::max(revision(name), base) + 1;
}

QStringList TemplateLibrary::projectsUsing(const QString& name) const { return ProjectRegistry::usingTemplate(name); }

bool TemplateLibrary::acceptName(const QString& wanted, QString* clean, QString* error, const QString& except) const {
    *clean = wanted.trimmed();
    clean->replace('#', '_');  // '#' separates a template's name from its copy's number
    const QString taken = holder(*clean, except);
    QString why;
    if (clean->isEmpty()) why = "Give the template a name.";
    else if (!taken.isEmpty()) why = "A template named " + taken + " already exists. Choose another name.";
    if (error && !why.isEmpty()) *error = why;
    return why.isEmpty();
}

// Names that differ only in signs ("a b", "a_b") share a file: the name is taken as soon as its file is.
QString TemplateLibrary::holder(const QString& name, const QString& except) const {
    const QString target = file(name);
    for (const gps::Graph& plan : plans_) {
        const QString other = fromStd(plan.name);
        if (file(other) == target) return other == except ? QString() : other;
    }
    return QFileInfo::exists(target) ? QFileInfo(target).fileName() : QString();
}

SaveResult TemplateLibrary::write(const QString& wanted, gps::Graph content, const QString& replacing) {
    QString name, error;
    if (!acceptName(wanted, &name, &error, replacing)) return SaveResult::failed(error);
    content.name = toStd(name);
    const QString target = file(name);
    QString historyError;
    if (!VersionHistory::keep(target, QByteArray::fromStdString(content.toConf()), &historyError))
        return SaveResult::failed("Unable to save the template: " + historyError);
    std::string failure;
    if (!content.save(toStd(target), &failure)) return SaveResult::failed("Unable to save the template: " + fromStd(failure));
    return SaveResult::written(target, content.revision);
}

SaveResult TemplateLibrary::save(const QString& name, gps::Graph content) {
    const SaveResult r = write(name, std::move(content), name);
    if (r.ok) reload();
    return r;
}

QString TemplateLibrary::nameError(const QString& name, const QString& wanted) const {
    QString clean, error;
    acceptName(wanted, &clean, &error, name);
    return error;
}

bool TemplateLibrary::rename(const QString& from, const QString& to, QString* clean, QString* error) {
    const gps::Graph* old = plan(from);
    if (!old) {
        if (error) *error = "There is no template named " + from + ".";
        return false;
    }
    if (!acceptName(to, clean, error, from)) return false;
    if (*clean == from) return true;
    const QString read = file(from);
    if (const SaveResult r = write(*clean, *old, from); !r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (read != file(*clean)) QFile::remove(read);  // "a b" and "a_b" share a file: it now holds the new name
    reload();
    emit renamed(from, *clean);
    return true;
}

bool TemplateLibrary::remove(const QString& name) {
    if (!plan(name) || !QFile::moveToTrash(file(name))) return false;
    reload();
    return true;
}

QString TemplateLibrary::removalNote(const QString& name) const {
    const int projects = projectsUsing(name).size();
    return projects ? QString("%1 project(s) use it; each keeps its own copy.").arg(projects) : QString();
}

bool TemplateLibrary::exportTo(const QString& name, const QString& file, QString* error) const {
    const gps::Graph* t = plan(name);
    std::string failure;
    if (t && t->save(toStd(file), &failure)) return true;
    if (error) *error = t ? fromStd(failure) : "There is no template named " + name + ".";
    return false;
}

QString TemplateLibrary::importFrom(const QString& file, QString* error) {
    gps::Graph t;
    std::string failure;
    if (!gps::Graph::load(toStd(file), &t, &failure, {toStd(AppPaths::templates())}) || t.elements.empty()) {
        if (error) *error = "Not a template: " + (failure.empty() ? QString("it holds no elements") : fromStd(failure));
        return QString();
    }
    QString name;
    const QString wanted = t.name.empty() || t.name == "pipeline" ? QFileInfo(file).completeBaseName() : fromStd(t.name);
    if (!acceptName(wanted, &name, error)) return QString();
    const SaveResult r = save(name, t);
    if (!r.ok && error) *error = r.error;
    return r.ok ? name : QString();
}
