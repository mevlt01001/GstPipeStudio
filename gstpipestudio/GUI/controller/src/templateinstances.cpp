// templateinstances.cpp – see templateinstances.h.
#include "templateinstances.h"

#include "capstext.h"
#include "documentviews.h"
#include "editsession.h"
#include "factorycatalog.h"
#include "names.h"
#include "pipeline.h"
#include "templatelibrary.h"
#include "templates.h"

#include <utility>

using Edit = EditSession::Edit;

TemplateInstances::TemplateInstances(EditSession& edits, const gps::Pipeline& pipeline, TemplateLibrary& library,
                                     const FactoryCatalog& catalog, const DocumentViews& views, QObject* parent)
    : QObject(parent), edits_(edits), pipeline_(pipeline), library_(library), catalog_(catalog), views_(views) {
    offerTimer_.setSingleShot(true);
    connect(&offerTimer_, &QTimer::timeout, this, &TemplateInstances::offerNow);
    // Any tab wrote the library: the newer versions of the copies here are offered.
    connect(&library_, &TemplateLibrary::changed, this, &TemplateInstances::offer);
    connect(&library_, &TemplateLibrary::renamed, this, &TemplateInstances::followRename);
}

const gps::Graph& TemplateInstances::plan() const { return pipeline_.editor().view(); }

void TemplateInstances::setExpanded(const QString& id, bool expanded) {
    if (expanded) expanded_.insert(id);
    else expanded_.remove(id);
}

// ---- making a template

QString TemplateInstances::begin(const QStringList& elements) {
    const QStringList members = views_.outline().expand(elements);
    if (members.isEmpty()) return QString();
    QString id;
    const bool made = edits_.transact([&](gps::Graph& g, Edit& edit) {
        std::vector<std::string> names;
        for (const QString& n : members) names.push_back(toStd(n));
        id = fromStd(gps::templates::wrap(g, names, "New template", {}));
        edit.action = QString("Made a template of %1 element(s)").arg(members.size());
        edit.presentation = true;
        return true;
    });
    return made ? id : QString();
}

bool TemplateInstances::finish(const QString& id, const QString& name, const std::vector<gps::ExposedParam>& params, QString* error) {
    QString clean;
    if (!library_.acceptName(name, &clean, error) || !onCanvas(id, error)) return false;
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        gps::TemplateInstance* t = g.instance(toStd(id));
        t->templateName = toStd(clean);
        t->params.clear();
        for (const gps::ExposedParam& p : params) {
            if (gps::templates::inside(g, p.element, t->id)) t->params.push_back(p);
        }
        gps::templates::sync(g, t->id, 1);
        edit.action = "Saved template " + clean;
        edit.presentation = true;
        return true;
    });
    return write(clean, id, error);
}

bool TemplateInstances::detach(const QString& id) {
    if (id.isEmpty() || !plan().instance(toStd(id))) return false;
    return edits_.transact([&](gps::Graph& g, Edit& edit) {
        edit.action = "Detached from template " + fromStd(views_.names().instance(toStd(id)));
        gps::templates::detach(g, toStd(id));
        expanded_.remove(id);
        edit.presentation = true;
        return true;
    });
}

std::vector<gps::ExposedParam> TemplateInstances::parameterChoices(const QStringList& elements) const {
    std::vector<gps::ExposedParam> out;
    const gps::Names names = views_.names();
    auto offer = [&](const std::string& element, const std::string& property) {
        gps::ExposedParam p{"", element, property};
        p.name = names.param(p);
        out.push_back(p);
    };
    for (const QString& member : elements) {
        const gps::Element* e = plan().find(toStd(member));
        if (!e) continue;
        if (const ElementInfo* inf = catalog_.info(fromStd(e->factory))) {
            for (const PropertyInfo& p : inf->properties) {
                if (p.writable && !p.deprecated) offer(e->name, p.name);
            }
        }
        // Fixed caps are parameters too, field by field: the element's own caps property, and the caps on its src links.
        auto own = e->props.find("caps");
        if (own != e->props.end()) {
            for (const gps::CapsField& f : gps::CapsChoice::fromText(own->second).fields) offer(e->name, "caps." + f.name);
        }
        const std::vector<int> links = plan().linksFromElement(e->name);
        for (int i : links) {
            const gps::Connection& l = plan().links[static_cast<size_t>(i)];
            const std::string prefix = links.size() == 1 && own == e->props.end() ? "caps." : l.srcPad + ".caps.";
            for (const gps::CapsField& f : gps::CapsChoice::fromText(l.caps).fields) offer(e->name, prefix + f.name);
        }
    }
    return out;
}

// ---- a changed copy

void TemplateInstances::keepEditing(const QString& id) {
    editing_.insert(id);
    emit logLine("Editing template " + fromStd(plan().instance(toStd(id))->templateName) +
                 ": the library gets the change when it collapses or the file is saved");
}

bool TemplateInstances::saveAsNew(const QString& id, const QString& name, QString* error) {
    QString clean;
    if (!library_.acceptName(name, &clean, error) || !onCanvas(id, error)) return false;
    editing_.remove(id);
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        g.instance(toStd(id))->templateName = toStd(clean);
        gps::templates::sync(g, toStd(id), 1);
        edit.action = "Saved a copy as the new template " + clean;
        edit.presentation = true;
        return true;
    });
    return write(clean, id, error);
}

bool TemplateInstances::commit(const QString& id) {
    if (!editing_.remove(id)) return false;
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    if (!t || !gps::templates::modified(plan(), t->id)) return false;
    const QString name = fromStd(t->templateName);
    const int revision = library_.nextRevision(name, t->revision);
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        gps::templates::sync(g, toStd(id), revision);
        edit.action = QString("Updated template %1 to revision %2").arg(name).arg(revision);
        edit.presentation = true;
        return true;
    });
    if (QString error; !write(name, id, &error)) {
        emit notice(error);
        return false;
    }
    QStringList others;
    for (const gps::TemplateInstance& other : plan().instances) {
        if (fromStd(other.id) != id && fromStd(other.templateName) == name && !gps::templates::modified(plan(), other.id)) others << fromStd(other.id);
    }
    if (!others.isEmpty()) update(others);
    return true;
}

void TemplateInstances::commitAll() {
    for (const QString& id : QSet<QString>(editing_)) commit(id);
}

// ---- newer revisions

QStringList TemplateInstances::outdated() const {
    QStringList out;
    for (const gps::TemplateInstance& t : plan().instances) {
        const gps::Graph* library = library_.plan(fromStd(t.templateName));
        if (library && library->revision > t.revision) out << fromStd(t.id);
    }
    return out;
}

bool TemplateInstances::update(const QStringList& ids) {
    const bool done = edits_.transact([&](gps::Graph& g, Edit& edit) {
        bool any = false;
        for (const QString& id : ids) {
            const gps::TemplateInstance* t = g.instance(toStd(id));
            const gps::Graph* library = t ? library_.plan(fromStd(t->templateName)) : nullptr;
            if (library && gps::templates::replaceContent(g, toStd(id), *library)) any = true;
        }
        for (const QString& id : ids) {
            const std::vector<std::string> members = gps::templates::members(g, toStd(id));
            if (!members.empty() && edit.touched.isEmpty()) edit.touched = fromStd(members.front());
        }
        edit.action = QString("Took the library's newer version of %1 template(s)").arg(ids.size());
        return any;
    });
    if (done) emit updated();
    return done;
}

// Asked when the current edit or write is over, so the offer is about the canvas as it is then.
void TemplateInstances::offer() { offerTimer_.start(0); }

// Only a revision not offered yet asks: the library read again (the catalog arrived, another template was written)
// does not ask about the same revisions twice.
void TemplateInstances::offerNow() {
    const QStringList ids = outdated();
    bool fresh = false;
    for (const QString& id : ids) {
        const int revision = library_.plan(fromStd(plan().instance(toStd(id))->templateName))->revision;
        if (std::exchange(offered_[id], revision) != revision) fresh = true;
    }
    if (fresh) emit templatesOutdated(ids);
}

// ---- parameters

std::vector<gps::ExposedParam> TemplateInstances::params(const QString& id) const {
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    return t ? t->params : std::vector<gps::ExposedParam>();
}

bool TemplateInstances::setParams(const QString& id, const std::map<std::string, std::string>& values) {
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    if (!t) return false;
    const QString title = fromStd(t->templateName);
    return edits_.transact([&](gps::Graph& g, Edit& edit) {
        for (const gps::ExposedParam& p : g.instance(toStd(id))->params) {
            auto value = values.find(p.name);
            if (value != values.end()) gps::templates::setParam(g, p, value->second);
        }
        edit.action = "Set the parameters of template " + title;
        edit.touched = views_.outline().membersOf(id).value(0);
        return true;
    });
}

void TemplateInstances::setTemplateParameters(const std::vector<gps::ExposedParam>& params) {
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        g.exposed.clear();
        for (const gps::ExposedParam& p : params) {
            if (g.find(p.element)) g.exposed.push_back(p);
        }
        edit.action = QString("Set %1 template parameter(s)").arg(g.exposed.size());
        edit.presentation = true;
        return true;
    });
}

void TemplateInstances::followRename(const QString& from, const QString& to) {
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        bool any = false;
        for (gps::TemplateInstance& t : g.instances) {
            if (t.templateName == toStd(from)) t.templateName = toStd(to), any = true;
        }
        edit.action = "Renamed template " + from + " to " + to;
        edit.presentation = true;
        return any;
    });
}

// ---- what changed

void TemplateInstances::check(bool ask) {
    QSet<QString> now;
    for (const gps::TemplateInstance& t : plan().instances) {
        if (gps::templates::modified(plan(), t.id)) now.insert(fromStd(t.id));
    }
    const QSet<QString> fresh = now - knownModified_ - editing_;
    knownModified_ = now;
    if (!ask || fresh.isEmpty()) return;
    QString innermost;
    size_t depth = 0;
    for (const QString& id : fresh) {
        const size_t d = gps::templates::chain(plan(), toStd(id)).size() + 1;
        if (d > depth || (d == depth && id < innermost)) depth = d, innermost = id;
    }
    QTimer::singleShot(0, this, [this, innermost] {
        if (plan().instance(toStd(innermost)) && !editing_.contains(innermost)) emit templateChanged(innermost);
    });
}

void TemplateInstances::forget() {
    editing_.clear();
    offered_.clear();
    check(false);
}

bool TemplateInstances::onCanvas(const QString& id, QString* error) const {
    if (plan().instance(toStd(id))) return true;
    if (error) *error = "The template is no longer on the canvas.";
    return false;
}

bool TemplateInstances::write(const QString& name, const QString& id, QString* error) {
    const SaveResult r = library_.save(name, gps::templates::extract(plan(), toStd(id)));
    if (!r.ok && error) *error = r.error;
    return r.ok;
}
