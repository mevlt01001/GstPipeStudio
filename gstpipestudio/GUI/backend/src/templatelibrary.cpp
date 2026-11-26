// templatelibrary.cpp – see templatelibrary.h.
#include "templatelibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

#include "apppaths.h"
#include "contract.h"
#include "factorycatalog.h"
#include "projectfile.h"
#include "templates.h"
#include "versionhistory.h"

TemplateLibrary::TemplateLibrary(FactoryCatalog& catalog, QObject* parent) : ComponentProvider(parent), catalog_(catalog) {
    connect(&catalog, &FactoryCatalog::ready, this, &TemplateLibrary::reload);
    reload();
}

// Until 0.3 the library lived in the config folder as custom_elements/; its files move once.
QString TemplateLibrary::directory() {
    const QString library = AppPaths::templates();
    QDir old(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_elements");
    if (qEnvironmentVariableIsEmpty("GPS_USER_DIR") && old.exists()) {
        for (const QString& f : old.entryList({"*.conf"}, QDir::Files)) QFile::rename(old.filePath(f), QDir(library).filePath(f));
        old.rmdir(".");
    }
    return library;
}

// The same file name a conf's template reference is looked up by.
QString TemplateLibrary::fileFor(const QString& name) {
    return QDir(directory()).filePath(fromStd(gps::templates::fileName(toStd(name))));
}

QString TemplateLibrary::file(const QString& name) const {
    auto found = files_.find(name);
    return found == files_.end() ? fileFor(name) : found->second;
}

void TemplateLibrary::reload() {
    names_.clear();
    plans_.clear();
    files_.clear();
    QDir dir(directory());
    for (const QString& f : dir.entryList({"*.conf"}, QDir::Files, QDir::Name)) {
        gps::Graph sub;
        std::string err;
        if (!gps::Graph::load(toStd(dir.filePath(f)), &sub, &err) || sub.elements.empty()) continue;
        // Library files before 0.4 tagged every element with "<name>#0": that is the template itself, not one inside it.
        for (const gps::TemplateInstance& t : std::vector<gps::TemplateInstance>(sub.instances)) {
            if (t.parent.empty() && t.templateName == sub.name) gps::templates::detach(sub, t.id);
        }
        const QString name = sub.name.empty() ? QFileInfo(f).completeBaseName() : fromStd(sub.name);
        plans_[name] = sub;
        files_[name] = dir.filePath(f);
        names_ << name;
    }
    describe();
    emit changed();
}

// A template's ports and role come from its factories' definitions, which the catalog has only for the factories some
// tab asked about. Once the catalog has its list, the library asks for the rest; a factory not installed is left out.
void TemplateLibrary::describe() {
    if (!catalog_.loaded()) return;
    QStringList missing;
    for (const auto& [name, content] : plans_) {
        for (const gps::Element& e : content.elements) {
            const QString factory = fromStd(e.factory);
            if (!catalog_.definition(factory) && catalog_.entry(factory) && !missing.contains(factory)) missing << factory;
        }
    }
    catalog_.fetchInfos(missing, this, [](bool) {});
}

// A template is described as one node: its pads are those its members leave free. A factory the catalog has not
// described yet gives no pads, so such a template may still take an input.
QList<ComponentInfo> TemplateLibrary::components() const {
    QList<ComponentInfo> out;
    for (const QString& name : names_) {
        const gps::Graph& content = plans_.at(name);
        QStringList factories;
        bool known = true;
        std::vector<std::string> all;
        for (const gps::Element& e : content.elements) {
            factories << fromStd(e.factory);
            known = known && catalog_.definition(fromStd(e.factory));
            all.push_back(e.name);
        }
        const gps::NodeShape shape(content, [this](const std::string& factory) { return catalog_.definition(fromStd(factory)); });
        const gps::NodePorts ports = shape.ports(all);
        ComponentInfo info;
        info.ref = ref(name);
        info.title = name;
        info.description = factories.join(" → ");
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
    if (!tmpl || tmpl->elements.empty()) return std::nullopt;
    const auto [left, top] = tmpl->topLeft();
    return gps::NodeRef::instance(gps::templates::insertTemplate(g, *tmpl, x - left, y - top));
}

QList<ComponentAction> TemplateLibrary::actions(const QString& name) const {
    if (!plan(name)) return {};
    return {ComponentAction::Edit, ComponentAction::Rename, ComponentAction::Export, ComponentAction::Delete};
}

const gps::Graph* TemplateLibrary::plan(const QString& name) const {
    auto it = plans_.find(name);
    return it == plans_.end() ? nullptr : &it->second;
}

gps::Graph TemplateLibrary::content(const QString& name) const {
    const gps::Graph* found = plan(name);
    return found ? *found : gps::Graph();
}

int TemplateLibrary::nextRevision(const QString& name, int base) const {
    const gps::Graph* found = plan(name);
    return std::max(found ? found->revision : 0, base) + 1;
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
    const QString target = fileFor(name);
    for (const auto& [other, path] : files_) {
        if (other != except && (fileFor(other) == target || path == target)) return other;
    }
    return QString();
}

SaveResult TemplateLibrary::write(const QString& name, gps::Graph content) {
    content.name = toStd(name);
    const QString target = fileFor(name), read = file(name);
    VersionHistory::keep(target, QByteArray::fromStdString(content.toConf()));  // a template keeps its history too
    std::string failure;
    if (!content.save(toStd(target), &failure)) return SaveResult::failed("Unable to save the template: " + fromStd(failure));
    if (read != target) QFile::remove(read);  // read from a file named the older way: one file for it from now on
    return SaveResult::written(target, content.revision);
}

SaveResult TemplateLibrary::save(const QString& name, gps::Graph content) {
    const SaveResult r = write(name, std::move(content));
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
    if (const SaveResult r = write(*clean, *old); !r.ok) {
        if (error) *error = r.error;
        return false;
    }
    if (read != fileFor(*clean)) QFile::remove(read);  // "a b" and "a_b" share a file: it now holds the new name
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
    if (!gps::Graph::load(toStd(file), &t, &failure, {toStd(directory())}) || t.elements.empty()) {
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
