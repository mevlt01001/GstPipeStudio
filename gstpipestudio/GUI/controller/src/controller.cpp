// controller.cpp – see controller.h. Rules come from gps_pipeline; this file only sequences them with the helpers.
#include "controller.h"

#include "capstext.h"
#include "actions.h"
#include "projectfile.h"
#include "templates.h"
#include "versionhistory.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRectF>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <csignal>

namespace {

// "video_%u" and 1 -> "video_1"
QString fillTemplate(const QString& name, int k) {
    QString out = name;
    out.replace("%u", QString::number(k)).replace("%d", QString::number(k));
    return out;
}

QString shellQuote(const QString& s) {
    static const QRegularExpression plain("^[A-Za-z0-9_./=:+-]+$");
    return plain.match(s).hasMatch() ? s : "'" + QString(s).replace("'", "'\\''") + "'";
}

// The chip on a link: "caps" and the number of pinned fields. The full text is the tooltip: the gap between
// two element boxes is only 40 px wide.
QString badgeOf(const QString& caps) {
    size_t pinned = gps::CapsChoice::fromText(toStd(caps)).fields.size();
    return pinned == 0 ? QString("caps") : QString("caps · %1").arg(pinned);
}

// At most metrics::kMaxPropLines lines; the last says how many more there are.
QStringList limitedLines(const QStringList& lines) {
    if (lines.size() <= metrics::kMaxPropLines) return lines;
    QStringList shown = lines.mid(0, metrics::kMaxPropLines - 1);
    return shown << QString("… +%1 more").arg(lines.size() - shown.size());
}

}  // namespace

Controller::Controller(AppServices& services, QObject* parent)
    : QObject(parent), services_(services), layout_(new CanvasLayout(*this)), watch_(new SlowWatch(this)) {
    bool ok = false;
    int ms = qEnvironmentVariableIntValue("GPS_UPDATE_TIMEOUT_MS", &ok);
    if (ok && ms > 0) updateTimeoutMs_ = ms;
    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(35);
    connect(&updateTimer_, &QTimer::timeout, this, &Controller::dispatchUpdate);
    connect(&backend_, &HelperExecutionBackend::logLine, this, [this](const QString& line) { log("[gps-update] " + line); });
    connect(watch_, &SlowWatch::slow, this, [this] {
        log("Validation is taking longer than expected");
        emit changed();
    });
    // The shared catalog already knows the factories other documents asked for; the Pipeline learns them now.
    catalog().forEachInfo([this](const QString& factory, const ElementInfo&) { registerFactory(factory); });
    connect(&catalog(), &FactoryCatalog::infoArrived, this, &Controller::registerFactory);
    connect(&catalog(), &FactoryCatalog::notice, this, &Controller::notice);
    connect(&catalog(), &FactoryCatalog::logLine, this, logger());
    connect(&services_.environment(), &EnvironmentService::checked, this, [this](const StartupResult& r) {
        log(r.state == StartupResult::Ready ? r.statusText() : "GStreamer check: the environment is not ready");
    });
    // Any tab wrote the library: this canvas shows it, and offers the newer versions of its copies.
    connect(&services_.library(), &TemplateLibrary::changed, this, [this] {
        emit changed();
        offerNewerTemplates();
    });
    offerTimer_.setSingleShot(true);
    connect(&offerTimer_, &QTimer::timeout, this, [this] {
        const QStringList outdated = outdatedTemplates();
        if (!outdated.isEmpty()) emit templatesOutdated(outdated);
    });
}

void Controller::registerFactory(const QString& factory) {
    pipeline_.registerFactory(toStd(factory), gps::definitionOf(*catalog().info(factory)));
}

// ---- state

bool Controller::canSave() const { return saveBlockedReason().isEmpty(); }

bool Controller::canRun() const { return runBlockedReason().isEmpty(); }

QString Controller::saveBlockedReason() const {
    if (plan().empty()) return "The canvas is empty";
    if (updating_) return "Validation is running";
    if (running_) return "The pipeline is running";
    if (monitoring_) return "Monitoring is running";
    if (opening_) return "A file is being opened";
    if (cancelled_) return "Validation was cancelled — press Validate";
    // A library template has no source of its own to validate it with: it is validated where it is used.
    if (!libraryTemplate_.isEmpty()) return QString();
    if (!result().valid()) return "The pipeline is not validated yet — press Validate";
    if (!result().ok()) return "The last validation reported a problem (see the Validation tab)";
    return QString();
}

QString Controller::runBlockedReason() const {
    if (!libraryTemplate_.isEmpty()) return "A template runs inside a pipeline: add it to one with Add template";
    QString reason = saveBlockedReason();
    if (reason.isEmpty() && !result().complete()) return "The pipeline is not complete: link every free src pad";
    return reason;
}

bool Controller::helperStopped() const { return result().status == "crashed" || result().status == "stopped"; }

std::function<void(const QString&)> Controller::logger() {
    return [this](const QString& line) { log(line); };
}

void Controller::log(const QString& line) {
    logTail_ << line;
    if (logTail_.size() > 40) logTail_.removeFirst();
    emit logLine(line);
}

void Controller::note(const QString& action, const QString& linking) {
    action_ = action;
    linking_ = linking;
}

QString Controller::padText(const QString& element, const QString& identity) const {
    const gps::Element* e = plan().find(toStd(element));
    return element + "." + identity + (e ? " (" + fromStd(e->factory) + ")" : QString());
}

SupportInfo Controller::supportInfo(const QString& title) const {
    SupportInfo i = SupportReport::systemInfo();
    i.title = title;
    i.action = action_;
    i.linking = linking_;
    if (result().valid()) i.validation = statusText(result()) + (result().message.empty() ? QString() : " – " + fromStd(result().message));
    else if (cancelled_) i.validation = "cancelled";
    i.logTail = logTail_.mid(qMax(0, logTail_.size() - 15));
    const StartupResult& env = services_.environment().result();
    if (env.state == StartupResult::Ready || env.state == StartupResult::TooOld) i.gstreamer = env.version;
    if (env.state == StartupResult::Ready) i.nvidiaElements = env.nvidia ? env.nvidiaElements : 0;
    return i;
}

void Controller::requestSupport(const QString& title) {
    SupportInfo info = supportInfo(title);
    if (SupportReport::open(info, *opener_)) {
        log("Support request opened in the browser");
        return;
    }
    // No browser (a server, a container): the user pastes the report on the issue page.
    if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->setText(SupportReport::text(info));
    emit notice("No web browser was found. The report was copied to the clipboard: paste it at " + SupportReport::issueUrl());
}

QString Controller::launchCommand() const {
    if (path_.isEmpty()) return QString();
    QFileInfo file(path_);
    QString text = "GPS_PIPELINE_PATH=" + shellQuote(file.absolutePath()) + " gst-launch-1.0 " + shellQuote(file.completeBaseName());
    for (const gps::ExposedParam& p : plan().exposed) text += " " + fromStd(p.name) + "=VALUE";
    return text;
}

// ---- templates & groups

// The box an element is drawn in: the outermost collapsed instance around it, "" when every one around it is expanded.
QString Controller::templateBox(const QString& elementName) const {
    const std::vector<std::string> around = gps::templates::chain(plan(), toStd(elementName));
    for (auto up = around.rbegin(); up != around.rend(); ++up) {
        if (!isTemplateExpanded(fromStd(*up))) return fromStd(*up);
    }
    return QString();
}

QStringList Controller::templateMembers(const QString& groupId) const {
    QStringList out;
    if (!groupId.isEmpty()) {
        for (const std::string& m : gps::templates::members(plan(), toStd(groupId))) out << fromStd(m);
    }
    return out;
}

QStringList Controller::expandSelection(const QStringList& elementNames) const {
    QStringList out;
    QSet<QString> seen;
    for (const QString& n : elementNames) {
        if (!plan().find(toStd(n))) continue;
        QString grp = templateBox(n);
        if (!grp.isEmpty()) {
            for (const QString& m : templateMembers(grp)) {
                if (!seen.contains(m)) {
                    seen.insert(m);
                    out << m;
                }
            }
        } else if (!seen.contains(n)) {
            seen.insert(n);
            out << n;
        }
    }
    return out;
}

void Controller::setTemplateExpanded(const QString& groupId, bool expanded) {
    if (groupId.isEmpty() || isTemplateExpanded(groupId) == expanded) return;
    if (!expanded && editingTemplates_.contains(groupId)) commitTemplateEdit(groupId);  // collapsing ends its editing
    // The box grows into its frame from its centre, or the frame shrinks into it; everything around it makes room.
    const CanvasLayout::Pin pin = layout_->pinOf(templateMembers(groupId), true);
    if (expanded) expandedTemplates_.insert(groupId);
    else expandedTemplates_.remove(groupId);
    layout_->invalidate();
    layout_->align({}, pin);
    emit changed();
}

bool Controller::detachTemplate(const QString& groupId) {
    QStringList members = templateMembers(groupId);
    if (groupId.isEmpty() || members.isEmpty()) return false;
    return transact([&](gps::Graph& g, Edit& edit) {
        gps::templates::detach(g, toStd(groupId));
        expandedTemplates_.remove(groupId);
        edit.action = "Detached from template " + groupId.section('#', 0, 0);
        edit.presentation = true;
        return true;
    });
}

QString Controller::beginTemplate(const QStringList& elementNames) {
    const QStringList members = expandSelection(elementNames);
    if (members.isEmpty()) return QString();
    QString id;
    const bool made = transact([&](gps::Graph& g, Edit& edit) {
        std::vector<std::string> names;
        for (const QString& n : members) names.push_back(toStd(n));
        id = fromStd(gps::templates::wrap(g, names, "New template", {}));
        edit.action = QString("Made a template of %1 element(s)").arg(members.size());
        edit.presentation = true;
        return true;
    });
    if (!made) return QString();
    setTemplateExpanded(id, true);  // its border is drawn, and the canvas makes room
    return id;
}

bool Controller::finishTemplate(const QString& id, const QString& name, const std::vector<gps::ExposedParam>& params, QString* error) {
    QString clean;
    if (!services_.library().acceptName(name, &clean, error) || !onCanvas(id, error)) return false;
    transact([&](gps::Graph& g, Edit& edit) {
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
    return services_.library().save(clean, gps::templates::extract(plan(), toStd(id)), error);
}

void Controller::cancelTemplate(const QString& id) { detachTemplate(id); }

bool Controller::renameTemplate(const QString& from, const QString& to, QString* error) {
    QString clean;
    if (!services_.library().rename(from, to, &clean, error)) return false;
    if (clean == from) return true;
    transact([&](gps::Graph& g, Edit& edit) {
        bool any = false;
        for (gps::TemplateInstance& t : g.instances) {
            if (t.templateName == toStd(from)) t.templateName = toStd(clean), any = true;
        }
        edit.action = "Renamed template " + from + " to " + clean;
        edit.presentation = true;
        return any;
    });
    return true;
}

void Controller::editLibraryTemplate(const QString& name) {
    if (!services_.library().plan(name)) return;
    const QString file = TemplateLibrary::fileFor(name);
    load(file, file, false, "Opened template " + name, name);
}

void Controller::setTemplateParameters(const std::vector<gps::ExposedParam>& params) {
    transact([&](gps::Graph& g, Edit& edit) {
        g.exposed.clear();
        for (const gps::ExposedParam& p : params) {
            if (g.find(p.element)) g.exposed.push_back(p);
        }
        edit.action = QString("Set %1 template parameter(s)").arg(g.exposed.size());
        edit.presentation = true;
        return true;
    });
}

bool Controller::saveTemplate(const QString& templateName, const QStringList& elementNames) {
    const QString id = beginTemplate(elementNames);
    if (id.isEmpty()) return false;
    QString error;
    if (!finishTemplate(id, templateName, {}, &error)) {
        emit notice(error);
        cancelTemplate(id);
        return false;
    }
    setTemplateExpanded(id, false);
    return true;
}

std::vector<gps::ExposedParam> Controller::parameterChoices(const QStringList& elements) const {
    std::vector<gps::ExposedParam> out;
    auto offer = [&](const std::string& element, const std::string& property) {
        gps::ExposedParam p{"", element, property};
        p.name = gps::templates::paramName(plan(), p);
        out.push_back(p);
    };
    for (const QString& member : elements) {
        const gps::Element* e = plan().find(toStd(member));
        if (!e) continue;
        if (const ElementInfo* inf = catalog().info(fromStd(e->factory))) {
            for (const PropertyInfo& p : inf->properties) {
                if (p.writable && !p.deprecated) offer(e->name, p.name);
            }
        }
        // Fixed caps are parameters too, field by field: the element's own caps property, and the caps on its src links.
        auto own = e->props.find("caps");
        if (own != e->props.end()) {
            for (const gps::CapsField& f : gps::CapsChoice::fromText(own->second).fields) offer(e->name, "caps." + f.name);
        }
        const std::vector<int> out = plan().linksFromElement(e->name);
        for (int i : out) {
            const gps::Connection& l = plan().links[static_cast<size_t>(i)];
            const std::string prefix = out.size() == 1 && own == e->props.end() ? "caps." : l.srcPad + ".caps.";
            for (const gps::CapsField& f : gps::CapsChoice::fromText(l.caps).fields) offer(e->name, prefix + f.name);
        }
    }
    return out;
}

std::vector<gps::ExposedParam> Controller::templateParams(const QString& id) const {
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    return t ? t->params : std::vector<gps::ExposedParam>();
}

bool Controller::setTemplateParams(const QString& id, const std::map<std::string, std::string>& values) {
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    if (!t) return false;
    const QString title = fromStd(t->templateName);
    return transact([&](gps::Graph& g, Edit& edit) {
        for (const gps::ExposedParam& p : g.instance(toStd(id))->params) {
            auto value = values.find(p.name);
            if (value != values.end()) gps::templates::setParam(g, p, value->second);
        }
        edit.action = "Set the parameters of template " + title;
        edit.touched = templateMembers(id).value(0);
        return true;
    });
}

QStringList Controller::addTemplate(const QString& templateName, double x, double y) {
    const gps::Graph* tmpl = services_.library().plan(templateName);
    if (!tmpl || tmpl->elements.empty() || editingBlocked()) return {};
    if (!infosReady(*tmpl, [=] { addTemplate(templateName, x, y); })) return {};
    const QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : freeSpot();
    QStringList created;
    transact([&](gps::Graph& g, Edit& edit) {
        const QPointF offset = at - topLeft(*tmpl);
        const std::string id = gps::templates::insertTemplate(g, *tmpl, offset.x(), offset.y());
        for (const std::string& m : gps::templates::members(g, id)) created << fromStd(m);
        edit.action = "Added template " + templateName;
        edit.touched = created.value(0);
        return true;
    });
    if (!created.isEmpty()) niceAlign();  // the new box finds its place among the others
    return created;
}

QStringList Controller::addTemplateAfter(const QString& srcElement, const QString& srcPad, const QString& templateName) {
    const gps::Graph* tmpl = services_.library().plan(templateName);
    const gps::Element* from = plan().find(toStd(srcElement));
    if (!tmpl || !from || editingBlocked()) return {};
    if (!infosReady(*tmpl, [=] { addTemplateAfter(srcElement, srcPad, templateName); })) return {};
    QStringList created;
    transact([&](gps::Graph& g, Edit& edit) {
        const QPointF offset = QPointF(from->x + metrics::kColumn, from->y) - topLeft(*tmpl);
        const QString groupId = fromStd(gps::templates::insertTemplate(g, *tmpl, offset.x(), offset.y()));
        for (const std::string& m : gps::templates::members(g, toStd(groupId))) created << fromStd(m);
        // srcElement.srcPad feeds the first free sink pad of the new group
        for (const PadView& p : templateView(groupId).sinks) {
            if (p.state != PadView::Linked) {
                g.linkExisting(toStd(srcElement), toStd(srcPad), toStd(p.element), toStd(p.templateName));
                break;
            }
        }
        edit.action = "Added template " + templateName + " after " + srcElement + "." + srcPad;
        edit.touched = created.value(0);
        return true;
    });
    return created;
}

// ---- views

QString Controller::elementNote(const QString& element, bool* isError) const {
    *isError = false;
    const UpdateResult& r = result();
    if (r.valid() && !r.ok() && fromStd(r.errorElement) == element) {
        *isError = true;
        return fromStd(r.message);
    }
    for (const std::vector<std::string>* list : {&r.refused, &r.missing}) {
        for (const std::string& text : *list) {
            const QString entry = fromStd(text);
            if (entry.startsWith(element + ".") || entry.startsWith(element + ":")) return entry;
        }
    }
    return QString();
}

QString Controller::displayName(const QString& element) const {
    const auto names = plan().displayNames();
    const auto it = names.find(toStd(element));
    return it == names.end() ? element : fromStd(it->second);
}

ElementView Controller::view(const QString& name) const {
    ElementView v;
    v.name = name;
    const gps::Element* element = plan().find(toStd(name));
    if (!element) return v;

    v.factory = fromStd(element->factory);
    v.displayTitle = displayName(name);
    v.groupId = fromStd(element->group);
    if (!v.groupId.isEmpty()) v.templateMembers = templateMembers(v.groupId);
    if (const FactoryEntry* e = catalog().entry(v.factory)) {
        v.nvidia = e->nvidia;
        v.longName = fromStd(e->longName);
    }

    // Changed properties, the exposed ones marked.
    QStringList lines;
    for (const auto& entry : element->props) {
        const auto& key = entry.first;
        const auto& value = entry.second;
        bool exposed = std::any_of(plan().exposed.begin(), plan().exposed.end(),
                                   [&](const gps::ExposedParam& p) { return p.element == element->name && p.property == key; });
        lines << (exposed ? "↗ " : "") + fromStd(key) + " = " + fromStd(value);
    }
    v.propLines = limitedLines(lines);
    v.note = elementNote(name, &v.isError);

    const ElementInfo* inf = catalog().info(v.factory);
    if (!inf) return v;
    v.isSource = inf->entry.isSource;
    v.isSink = inf->entry.isSink;
    v.isTracked = plan().isElementTracked(element->name, catalog().traits());
    if (!v.isTracked && v.note.isEmpty()) {
        v.isInfo = true;
        v.note = "ℹ Unlinked sink pad — not tracked until fed by a source.";
    }

    // Pads: the Pipeline owns which exist (Pipeline::padsOf); the view adds what the canvas shows of them.
    for (const gps::Pad& pad : element->pads) {
        const bool src = pad.direction == gps::Pad::Direction::Source;
        const PadTemplateInfo* t = inf->padTemplate(pad.templateName, src ? "src" : "sink");
        std::string templateName;
        int k = 0;
        gps::splitPadIdentity(pad.identity, &templateName, &k);
        PadView p;
        p.element = name;
        p.identity = fromStd(pad.identity);
        p.templateName = fromStd(pad.templateName);
        p.isSrc = src;
        p.label = !pad.nativeName.empty() ? fromStd(pad.nativeName)
                  : pad.identity == pad.templateName ? p.templateName : fillTemplate(p.templateName, k);
        if (src) {
            p.link = plan().linkFrom(element->name, pad.identity);
            const std::string& caps = pad.negotiatedCaps.empty() ? pad.allowedCaps : pad.negotiatedCaps;
            p.media = fromStd(gps::mediaOf(caps));
            p.tip = fromStd(caps);
            p.caps = fromStd(pad.negotiatedCaps);
            p.state = p.link >= 0 ? PadView::Linked : (!v.isTracked || pad.unknownData) ? PadView::Unknown : PadView::Free;
            v.srcs << p;
        } else {
            p.link = plan().linkInto(element->name, pad.identity);
            p.media = t && !t->structures.empty() ? fromStd(gps::structureTitle(t->structures.front())) : QString();
            p.tip = fromStd(pad.templateCaps);
            if (!pad.negotiatedCaps.empty()) {
                p.media = fromStd(gps::mediaOf(pad.negotiatedCaps));
                p.tip = p.caps = fromStd(pad.negotiatedCaps);
            }
            p.state = p.link >= 0 ? PadView::Linked : PadView::Empty;
            v.sinks << p;
        }
    }
    return v;
}

void Controller::subgraphExternalPads(const QStringList& members, QList<PadView>* outSinks, QList<PadView>* outSrcs) const {
    QSet<QString> memberSet(members.begin(), members.end());
    for (const QString& m : members) {
        ElementView mv = view(m);
        if (outSinks) {
            for (const PadView& sp : mv.sinks) {
                bool internal = sp.link >= 0 && sp.link < static_cast<int>(plan().links.size()) &&
                                memberSet.contains(fromStd(plan().links[sp.link].srcElement));
                if (!internal) *outSinks << sp;
            }
        }
        if (outSrcs) {
            for (const PadView& srp : mv.srcs) {
                bool internal = srp.link >= 0 && srp.link < static_cast<int>(plan().links.size()) &&
                                memberSet.contains(fromStd(plan().links[srp.link].sinkElement));
                if (!internal) *outSrcs << srp;
            }
        }
    }
}

ElementView Controller::templateView(const QString& groupId) const {
    ElementView v;
    QStringList members = templateMembers(groupId);
    if (members.isEmpty()) return v;

    const gps::TemplateInstance* placed = plan().instance(toStd(groupId));
    QString title = placed ? fromStd(placed->templateName) : groupId.section('#', 0, 0);
    v.name = members.first();
    v.displayTitle = title.isEmpty() ? groupId : title;
    v.groupId = groupId;
    v.isTemplate = true;
    v.templateMembers = members;
    v.isTracked = false;

    QStringList factories;
    QStringList propSummary;
    for (const QString& m : members) {
        ElementView mv = view(m);
        factories << mv.factory;
        if (mv.nvidia) v.nvidia = true;
        if (mv.isTracked) v.isTracked = true;
        if (mv.isError && !mv.note.isEmpty()) {
            v.isError = true;
            v.note = mv.displayTitle + ": " + mv.note;
        } else if (v.note.isEmpty() && !mv.note.isEmpty() && !mv.isInfo) {
            v.note = mv.displayTitle + ": " + mv.note;
        }
        for (const QString& pl : mv.propLines) {
            if (!pl.startsWith("…")) propSummary << (mv.displayTitle + "." + pl);
        }
    }
    subgraphExternalPads(members, &v.sinks, &v.srcs);
    v.isSource = v.sinks.isEmpty() && !v.srcs.isEmpty();
    v.isSink = !v.sinks.isEmpty() && v.srcs.isEmpty();
    v.factory = QString("template (%1) · %2").arg(members.size()).arg(factories.join(" → "));
    v.propLines = limitedLines(propSummary);
    if (!v.isTracked && v.note.isEmpty() && !v.isSource) {
        v.isInfo = true;
        v.note = "ℹ Unlinked template — not tracked until fed by a source.";
    }
    return v;
}

FrameView Controller::frameView(const QString& instanceId) const {
    FrameView v;
    const gps::TemplateInstance* t = plan().instance(toStd(instanceId));
    if (!t) return v;
    v.id = instanceId;
    v.title = fromStd(t->templateName);
    v.members = templateMembers(instanceId);
    v.depth = static_cast<int>(gps::templates::chain(plan(), t->id).size());
    QList<PadView> sinks, srcs;
    subgraphExternalPads(v.members, &sinks, &srcs);
    v.pads = sinks + srcs;
    return v;
}

QStringList Controller::visibleFrames() const {
    QStringList out;
    for (const gps::TemplateInstance& t : plan().instances) {
        bool shown = isTemplateExpanded(fromStd(t.id));
        for (std::string up = t.parent; shown && !up.empty(); up = plan().instance(up)->parent) shown = isTemplateExpanded(fromStd(up));
        if (shown) out << fromStd(t.id);
    }
    return out;
}

QString Controller::linkSinkIdentity(int link) const { return fromStd(plan().sinkIdentity(static_cast<size_t>(link))); }

QList<LinkView> Controller::linkViews() const {
    QList<LinkView> out;
    for (size_t i = 0; i < plan().links.size(); ++i) {
        const gps::Connection& l = plan().links[i];
        LinkView v;
        v.index = static_cast<int>(i);
        v.srcElement = fromStd(l.srcElement);
        v.srcIdentity = fromStd(l.srcPad);
        v.sinkElement = fromStd(l.sinkElement);
        v.sinkIdentity = linkSinkIdentity(v.index);
        v.id = CanvasLayout::linkId(v.srcElement, v.srcIdentity);
        v.route = layout_->route(v.id);
        v.badge = l.caps.empty() ? QString() : badgeOf(fromStd(l.caps));
        v.tip = fromStd(l.caps);
        const PadReport* srcRep = result().pad(l.srcElement, l.srcPad, true);
        if (srcRep && !srcRep->caps.empty()) {
            v.negotiatedCaps = fromStd(srcRep->caps);
        } else if (!l.caps.empty()) {
            v.negotiatedCaps = fromStd(l.caps);
        }
        if (v.tip.isEmpty() && !v.negotiatedCaps.isEmpty()) v.tip = v.negotiatedCaps;
        out << v;
    }
    return out;
}

std::vector<StructureInfo> Controller::padStructures(const QString& element, const QString& identity) const {
    const PadReport* r = result().pad(toStd(element), toStd(identity), true);
    return r ? r->allowedStructures : std::vector<StructureInfo>();
}

QString Controller::padAllowed(const QString& element, const QString& identity) const {
    const PadReport* r = result().pad(toStd(element), toStd(identity), true);
    return r ? fromStd(r->allowed) : QString();
}

const PadTemplateInfo* Controller::linkSinkTemplate(int link) const {
    if (link < 0 || link >= static_cast<int>(plan().links.size())) return nullptr;
    const gps::Connection& l = plan().links[link];
    const gps::Element* sink = plan().find(l.sinkElement);
    const ElementInfo* inf = sink ? catalog().info(fromStd(sink->factory)) : nullptr;
    return inf ? inf->padTemplate(l.sinkPad, "sink") : nullptr;
}

QList<ExistingTarget> Controller::existingTargets(const QString& element, const QString& identity) const {
    QList<ExistingTarget> out;
    std::vector<StructureInfo> allowed = padStructures(element, identity);
    for (const gps::Element& e : plan().elements) {
        const ElementInfo* inf = catalog().info(fromStd(e.factory));
        if (!inf || plan().wouldCycle(toStd(element), e.name)) continue;
        for (const PadTemplateInfo& t : inf->sinkTemplates()) {
            if (!gps::structuresMeet(allowed, t.structures)) continue;
            if (!t.isRequest() && plan().sinkPadTaken(e.name, t.name)) continue;
            out.append({fromStd(e.name), fromStd(e.factory), fromStd(t.name), t.isRequest()});
        }
    }
    return out;
}

std::vector<PadTemplateInfo> Controller::sinkChoices(const ElementInfo& inf, const QString& element, const QString& identity) const {
    return inf.sinkTemplatesFor(padStructures(element, identity));
}

// ---- layout

QPointF Controller::freeSpot() const {
    double y = 40;
    for (const gps::Element& e : plan().elements) y = std::max(y, e.y + view(fromStd(e.name)).height() + 40);
    return QPointF(40, y);
}

// Automatic placement moves only the newly created element. Native observations
// never rearrange a user's canvas.
void Controller::placeNewElement(const std::string& id) {
    auto* element = editGraph().find(id);
    if (!element) return;
    QRectF rect(element->x, element->y, metrics::kBoxWidth, view(fromStd(id)).height());
    bool moved;
    do {
        moved = false;
        for (const auto& other : plan().elements) {
            if (other.name == id) continue;
            QRectF occupied(other.x, other.y, metrics::kBoxWidth, view(fromStd(other.name)).height());
            if (rect.adjusted(-metrics::kGap/2, -metrics::kGap/2, metrics::kGap/2, metrics::kGap/2).intersects(occupied)) {
                rect.moveTop(occupied.bottom() + metrics::kGap);
                moved = true;
            }
        }
    } while (moved);
    element->x = rect.x(); element->y = rect.y();
}

// ---- edits

void Controller::applyEdit(const std::string& element, const ElementEdit& edit) {
    gps::Element* e = editGraph().find(element);
    if (!e) return;

    // Request pad counts, src and sink alike: every template the element or the edit names gets the edit's count.
    auto counts = [&](const std::map<std::string, int>& now, const std::map<std::string, int>& wanted,
                      bool (gps::Graph::*set)(const std::string&, const std::string&, int)) {
        std::set<std::string> templates;
        for (const auto& entry : now) templates.insert(entry.first);
        for (const auto& entry : wanted) templates.insert(entry.first);
        for (const std::string& t : templates) {
            auto found = wanted.find(t);
            int count = found == wanted.end() ? 0 : found->second;
            if (!(editGraph().*set)(element, t, count))
                emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
        }
    };
    counts(e->requestSrc, edit.requestSrc, &gps::Graph::setRequestSrc);
    counts(e->requestSink, edit.requestSink, &gps::Graph::setRequestSink);
}

bool Controller::transact(const std::function<bool(gps::Graph&, Edit&)>& change) {
    if (editingBlocked()) return false;
    pipeline_.editor().begin();
    Edit edit;
    if (!change(editGraph(), edit)) {
        pipeline_.editor().rollback();
        return false;
    }
    note(edit.action, edit.linking);
    if (edit.presentation) {
        if (!commitTransaction()) return false;
        dirty_ = true;
        emit changed();
        alignAfterEdit();
        checkTemplates(true);
        return true;
    }
    edited(edit.touched);
    checkTemplates(true);
    return true;
}

// Which template copies hold content their template does not: the first time one does, the window asks what that
// means (unless the user already chose to edit that template). Undo and opening a file only take note.
void Controller::checkTemplates(bool ask) {
    QSet<QString> now;
    for (const gps::TemplateInstance& t : plan().instances) {
        if (gps::templates::modified(plan(), t.id)) now.insert(fromStd(t.id));
    }
    QSet<QString> fresh = now - knownModified_ - editingTemplates_;
    knownModified_ = now;
    if (!ask || fresh.isEmpty()) return;
    QString innermost;
    size_t depth = 0;
    for (const QString& id : fresh) {
        const size_t d = gps::templates::chain(plan(), toStd(id)).size() + 1;
        if (d > depth || (d == depth && id < innermost)) depth = d, innermost = id;
    }
    QTimer::singleShot(0, this, [this, innermost] {
        if (plan().instance(toStd(innermost)) && !editingTemplates_.contains(innermost)) emit templateChanged(innermost);
    });
}

void Controller::keepEditingTemplate(const QString& id) {
    editingTemplates_.insert(id);
    log("Editing template " + fromStd(plan().instance(toStd(id))->templateName) + ": the library gets the change when it collapses or the file is saved");
}

// The edited copy becomes the template's next revision; the other copies on this canvas that were not changed follow.
bool Controller::commitTemplateEdit(const QString& id) {
    editingTemplates_.remove(id);
    const gps::TemplateInstance* t = plan().instance(toStd(id));
    if (!t || !gps::templates::modified(plan(), t->id)) return false;
    const QString name = fromStd(t->templateName);
    const int revision = services_.library().nextRevision(name, t->revision);
    transact([&](gps::Graph& g, Edit& edit) {
        gps::templates::sync(g, toStd(id), revision);
        edit.action = QString("Updated template %1 to revision %2").arg(name).arg(revision);
        edit.presentation = true;
        return true;
    });
    QString error;
    if (!services_.library().save(name, gps::templates::extract(plan(), toStd(id)), &error)) {
        emit notice(error);
        return false;
    }
    QStringList others;
    for (const gps::TemplateInstance& other : plan().instances) {
        if (fromStd(other.id) != id && fromStd(other.templateName) == name && !gps::templates::modified(plan(), other.id)) others << fromStd(other.id);
    }
    if (!others.isEmpty()) updateTemplates(others);
    emit changed();
    return true;
}

bool Controller::saveTemplateAsNew(const QString& id, const QString& name, QString* error) {
    QString clean;
    if (!services_.library().acceptName(name, &clean, error) || !onCanvas(id, error)) return false;
    editingTemplates_.remove(id);
    transact([&](gps::Graph& g, Edit& edit) {
        g.instance(toStd(id))->templateName = toStd(clean);
        gps::templates::sync(g, toStd(id), 1);
        edit.action = "Saved a copy as the new template " + clean;
        edit.presentation = true;
        return true;
    });
    return services_.library().save(clean, gps::templates::extract(plan(), toStd(id)), error);
}

bool Controller::onCanvas(const QString& id, QString* error) const {
    if (plan().instance(toStd(id))) return true;
    if (error) *error = "The template is no longer on the canvas.";
    return false;
}

// Asked when the current edit or write is over, so the offer is about the canvas as it is then.
void Controller::offerNewerTemplates() { offerTimer_.start(0); }

QStringList Controller::outdatedTemplates() const {
    QStringList out;
    for (const gps::TemplateInstance& t : plan().instances) {
        const gps::Graph* library = services_.library().plan(fromStd(t.templateName));
        if (library && library->revision > t.revision) out << fromStd(t.id);
    }
    return out;
}

bool Controller::updateTemplates(const QStringList& ids) {
    QStringList touched;
    const bool done = transact([&](gps::Graph& g, Edit& edit) {
        bool any = false;
        for (const QString& id : ids) {
            const gps::TemplateInstance* t = g.instance(toStd(id));
            const gps::Graph* library = t ? services_.library().plan(fromStd(t->templateName)) : nullptr;
            if (library && gps::templates::replaceContent(g, toStd(id), *library)) any = true;
        }
        for (const QString& id : ids) {
            for (const std::string& m : gps::templates::members(g, toStd(id))) touched << fromStd(m);
        }
        edit.action = QString("Took the library's newer version of %1 template(s)").arg(ids.size());
        edit.touched = touched.value(0);
        return any;
    });
    if (done) niceAlign();
    return done;
}

bool Controller::commitTransaction() {
    if (!pipeline_.editor().editing()) return true;
    std::string error;
    const bool committed = pipeline_.editor().commit(&error);
    if (!error.empty()) emit notice(fromStd(error));
    return committed || error.empty();
}

void Controller::edited(const QString& touchedElement) {
    if (!commitTransaction()) return;
    dirty_ = true;
    touched_ = touchedElement;
    emit changed();
    alignAfterEdit();
    if (pipeline_.lastChange().kind == gps::ChangeKind::Execution || updating_) {
        startUpdate();
    } else {
        emit updateFinished(true);
    }
}

bool Controller::undo() {
    if (!canUndo() || !pipeline_.editor().undo()) return false;
    note("Undo");
    const QScopedValueRollback restoring(restoring_, true);  // going back is not an edit to follow
    edited(QString());
    checkTemplates(false);
    return true;
}

bool Controller::redo() {
    if (!canRedo() || !pipeline_.editor().redo()) return false;
    note("Redo");
    const QScopedValueRollback restoring(restoring_, true);
    edited(QString());
    checkTemplates(false);
    return true;
}

gps::Graph Controller::copySubplan(const QStringList& elementNames) const {
    std::vector<std::string> ids;
    for (const auto& name : expandSelection(elementNames)) ids.push_back(toStd(name));
    return gps::GraphFragment(std::move(ids)).copy(plan());
}

QStringList Controller::pasteSubplan(const gps::Graph& clip, double targetX, double targetY) {
    if (editingBlocked() || clip.elements.empty()) return {};
    if (!infosReady(clip, [=] { pasteSubplan(clip, targetX, targetY); })) return {};
    const QPointF at = std::isfinite(targetX) ? QPointF(targetX, targetY) : topLeft(clip) + QPointF(40, 40);
    QStringList created;
    transact([&](gps::Graph& g, Edit& edit) {
        const QPointF offset = at - topLeft(clip);
        for (const auto& [old, made] : gps::templates::insertCopy(g, clip, offset.x(), offset.y())) {
            if (g.find(made)) created << fromStd(made);
        }
        edit.action = QString("Pasted %1 element(s)").arg(created.size());
        edit.touched = created.last();
        return true;
    });
    return created;
}

// The clip's factories are known (true), or are asked for and retry runs once they are (false).
bool Controller::infosReady(const gps::Graph& clip, std::function<void()> retry) {
    QStringList missing;
    for (const gps::Element& e : clip.elements) {
        QString factory = fromStd(e.factory);
        if (!catalog().info(factory) && !missing.contains(factory)) missing << factory;
    }
    if (missing.isEmpty()) return true;
    catalog().fetchInfos(missing, this, [retry](bool ok) {
        if (ok) retry();
    });
    return false;
}

QPointF Controller::topLeft(const gps::Graph& clip) {
    QPointF corner(clip.elements.front().x, clip.elements.front().y);
    for (const gps::Element& e : clip.elements) corner = QPointF(std::min(corner.x(), e.x), std::min(corner.y(), e.y));
    return corner;
}


QStringList Controller::duplicateElements(const QStringList& names) {
    if (editingBlocked() || names.isEmpty()) return {};
    gps::Graph sub = copySubplan(names);
    if (sub.elements.empty()) return {};
    return pasteSubplan(sub);
}

bool Controller::addElement(const QString& factory, const ElementEdit& edit, double x, double y) {
    if (!catalog().info(factory)) return false;
    return transact([&](gps::Graph& g, Edit& e) {
        QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : freeSpot();
        std::string name = g.addElement(toStd(factory), edit.props, at.x(), at.y());
        applyEdit(name, edit);
        e.action = "Added " + factory;
        e.touched = fromStd(name);
        return true;
    });
}

bool Controller::addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                          const ElementEdit& edit) {
    const gps::Element* from = plan().find(toStd(element));
    if (!catalog().info(factory) || !from) return false;
    return transact([&](gps::Graph& g, Edit& e) {
        std::string name = g.addAfter(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props,
                                      from->x + metrics::kColumn, from->y);
        if (name.empty()) return false;
        applyEdit(name, edit);
        placeNewElement(name);
        e.action = "Added " + factory + " after " + element + "." + pad;
        e.linking = padText(element, pad) + " -> " + padText(fromStd(name), sinkPad);
        e.touched = fromStd(name);
        return true;
    });
}

bool Controller::linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad) {
    if (editingBlocked()) return false;
    if (element == sinkElement || plan().wouldCycle(toStd(element), toStd(sinkElement))) {
        emit notice(fromStd(gps::Graph::ouroborosMessage(toStd(element), toStd(sinkElement))));
        return false;
    }
    return transact([&](gps::Graph& g, Edit& e) {
        if (!g.linkExisting(toStd(element), toStd(pad), toStd(sinkElement), toStd(sinkPad))) return false;
        e.action = "Linked " + element + "." + pad + " to an existing element";
        e.linking = padText(element, pad) + " -> " + padText(sinkElement, sinkPad);
        e.touched = sinkElement;
        return true;
    });
}

bool Controller::editElement(const QString& element, const ElementEdit& edit) {
    const gps::Element* found = plan().find(toStd(element));
    if (!found) return false;
    const QString factory = fromStd(found->factory);
    return transact([&](gps::Graph& g, Edit& e) {
        g.setProps(toStd(element), edit.props);
        applyEdit(toStd(element), edit);
        e.action = "Edited the properties of " + element + " (" + factory + ")";
        e.touched = element;
        return true;
    });
}

bool Controller::setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps) {
    if (plan().linkFrom(toStd(element), toStd(pad)) < 0) return false;
    return transact([&](gps::Graph& g, Edit& e) {
        g.setCaps(toStd(element), toStd(pad), toStd(caps));
        g.setSinkPadProps(toStd(element), toStd(pad), sinkPadProps);
        e = {"Changed the caps of " + element + "." + pad, padText(element, pad), element};
        return true;
    });
}

bool Controller::cut(const QString& element, const QString& pad) {
    const QString linking = padText(element, pad);
    return transact([&](gps::Graph& g, Edit& e) {
        e = {"Cut the link at " + element + "." + pad, linking, element};
        return g.cut(toStd(element), toStd(pad), catalog().traits()).links > 0;
    });
}

bool Controller::disconnectLink(const QString& element, const QString& pad) {
    if (editingBlocked()) return false;
    const QString linking = padText(element, pad);
    gps::DisconnectAction action(toStd(element), toStd(pad));
    if (!pipeline_.editor().execute(action)) return false;
    note("Disconnected the link at " + element + "." + pad, linking);
    edited(element);
    checkTemplates(true);
    return true;
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (!catalog().info(factory)) return false;
    const QString linking = padText(element, pad);
    return transact([&](gps::Graph& g, Edit& e) {
        gps::Addition added = g.replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, catalog().traits());
        if (added.element.empty()) return false;
        applyEdit(added.element, edit);
        e.action = "Replaced the element behind " + element + "." + pad + " with " + factory;
        e.linking = linking + " -> " + factory;
        e.touched = fromStd(added.element);
        return true;
    });
}

bool Controller::deleteSource(const QString& element) {
    return deleteSelection({element}, {}, false);
}

bool Controller::deleteElement(const QString& element, bool autoHeal) {
    return deleteSelection({element}, {}, autoHeal);
}

bool Controller::deleteSelection(const QStringList& elementNames, const QList<QPair<QString, QString>>& links, bool autoHeal) {
    QStringList expanded = expandSelection(elementNames);
    if (expanded.isEmpty() && links.isEmpty()) return false;
    QStringList touched = expanded;
    for (const auto& link : links) touched << link.first;

    QList<PadView> extSrcs;
    subgraphExternalPads(expanded, nullptr, &extSrcs);
    const int exitTotalSrcPads = extSrcs.isEmpty() ? 1 : extSrcs.size();
    std::vector<std::string> names;
    for (const QString& n : expanded) names.push_back(toStd(n));

    return transact([&](gps::Graph& g, Edit& e) {
        bool changedAny = false;
        for (const auto& [srcEl, srcPad] : links) changedAny = g.disconnectLink(toStd(srcEl), toStd(srcPad)) || changedAny;
        if (!names.empty()) changedAny = g.deleteElements(names, autoHeal, exitTotalSrcPads) || changedAny;
        e.action = expanded.size() == 1 ? "Deleted element " + expanded.first() : QString("Deleted selection (%1 elements)").arg(expanded.size());
        return changedAny;
    });
}

bool Controller::spliceElement(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad) {
    return spliceElements({element}, linkSrcElement, linkSrcPad);
}

bool Controller::spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad) {
    if (editingBlocked() || elements.isEmpty()) return false;
    QStringList expanded = expandSelection(elements);
    if (expanded.isEmpty()) return false;

    std::vector<std::string> fragmentIds;
    for (const auto& id : expanded) fragmentIds.push_back(toStd(id));
    if (!gps::GraphFragment(std::move(fragmentIds)).connected(plan())) {
        return false;
    }

    QList<PadView> extSinks, extSrcs;
    subgraphExternalPads(expanded, &extSinks, &extSrcs);

    std::vector<std::pair<std::string, std::string>> freeSinks, freeSrcs;
    for (const PadView& p : extSinks) {
        if (p.state != PadView::Linked) freeSinks.emplace_back(toStd(p.element), toStd(p.templateName));
    }
    if (expanded.size() == 1) {
        const QString& only = expanded.first();
        if (const ElementInfo* inf = catalog().info(view(only).factory)) {
            for (const PadTemplateInfo& t : inf->sinkTemplates()) {
                if (t.isRequest()) {
                    std::pair<std::string, std::string> cand{toStd(only), t.name};
                    if (std::find(freeSinks.begin(), freeSinks.end(), cand) == freeSinks.end()) {
                        freeSinks.push_back(cand);
                    }
                }
            }
        }
    }
    for (const PadView& p : extSrcs) {
        if (p.state != PadView::Linked) freeSrcs.emplace_back(toStd(p.element), toStd(p.identity));
    }

    std::vector<std::string> ids;
    for (const auto& id : expanded) ids.push_back(toStd(id));
    gps::InsertFragmentAction action(gps::GraphFragment(std::move(ids)),
        {toStd(linkSrcElement), toStd(linkSrcPad)}, std::move(freeSinks), std::move(freeSrcs));
    std::string error;
    if (!pipeline_.editor().execute(action, &error)) {
        if (!error.empty()) emit notice(fromStd(error));
        return false;
    }
    note("Spliced " + expanded.join(", ") + " into link from " + linkSrcElement + "." + linkSrcPad);
    edited(expanded.first());
    checkTemplates(true);
    return true;
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (!catalog().info(fromStd(gps::kQueueFactory))) return false;
    return transact([&](gps::Graph& g, Edit& e) {
        std::string name = g.insertQueue(toStd(element), toStd(pad));
        if (name.empty()) return false;
        makeRoomForQueue(toStd(element), name);
        e = {"Inserted a queue after " + element + "." + pad, padText(element, pad), fromStd(name)};
        return true;
    });
}

// Graph::insertQueue puts the queue half way between its neighbours, on top of both when they are one column apart.
// Here it takes the column after its source, on the row of the box it feeds, and that box and everything behind it
// move one column right, so every link keeps running left to right.
void Controller::makeRoomForQueue(const std::string& source, const std::string& queue) {
    int out = plan().linkFrom(queue, gps::kQueueSrcPad);
    gps::Element* from = editGraph().find(source);
    gps::Element* q = editGraph().find(queue);
    if (out < 0 || !from || !q) return;
    gps::Element* sink = editGraph().find(plan().links[out].sinkElement);
    if (!sink) return;
    q->x = from->x + metrics::kColumn;
    q->y = sink->y;
    double need = q->x + metrics::kColumn - sink->x;
    if (need <= 0) return;

    std::set<std::string> behind = {sink->name};
    for (bool grew = true; grew;) {
        grew = false;
        for (const gps::Connection& l : plan().links) {
            if (behind.count(l.srcElement) && behind.insert(l.sinkElement).second) grew = true;
        }
    }
    for (gps::Element& e : editGraph().elements) {
        if (behind.count(e.name)) e.x += need;
    }
}

bool Controller::removeQueue(const QString& queue) {
    return transact([&](gps::Graph& g, Edit& e) {
        e.action = "Removed queue " + queue;
        return g.removeQueue(toStd(queue));
    });
}

void Controller::setPosition(const QString& element, double x, double y) {
    setPositions({{element, QPointF(x, y)}});
}

void Controller::setPositions(const QList<QPair<QString, QPointF>>& positions) {
    if (positions.isEmpty() || editingBlocked()) return;
    QSet<QString> handledGroups;
    std::map<std::string, gps::Placement> updates;
    for (const auto& [element, pt] : positions) {
        const auto* e = plan().find(toStd(element));
        if (!e) continue;
        const double dx = pt.x() - e->x, dy = pt.y() - e->y;
        if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) continue;
        const QString group = templateBox(element);
        if (!group.isEmpty()) {
            if (handledGroups.contains(group)) continue;
            handledGroups.insert(group);
            for (const auto& member : templateMembers(group)) {
                const auto* other = plan().find(toStd(member));
                updates[other->name] = {other->name, other->x + dx, other->y + dy};
            }
        } else updates[e->name] = {e->name, pt.x(), pt.y()};
    }
    QList<QPair<QString, QPointF>> moves;
    for (const auto& entry : updates) moves.append({fromStd(entry.first), QPointF(entry.second.x, entry.second.y)});
    move(moves);
}

bool Controller::move(const QList<QPair<QString, QPointF>>& placements) {
    std::vector<gps::Placement> moves;
    for (const auto& [element, at] : placements) moves.push_back({toStd(element), at.x(), at.y()});
    if (moves.empty() || !pipeline_.editor().execute(gps::MoveElementsAction(std::move(moves)))) return false;
    emit changed();
    alignAfterEdit();  // with automatic alignment a dragged box slides back into place
    return true;
}

bool Controller::linkOrderedInternal(const QStringList& expanded, QString* lastTouched) {
    if (expanded.size() < 2) return false;
    std::vector<QString> sorted(expanded.begin(), expanded.end());
    std::stable_sort(sorted.begin(), sorted.end(), [this](const QString& a, const QString& b) {
        const gps::Element* ea = plan().find(toStd(a));
        const gps::Element* eb = plan().find(toStd(b));
        if (!ea || !eb) return a < b;
        if (std::abs(ea->x - eb->x) > 1.0) return ea->x < eb->x;
        ElementView va = view(a), vb = view(b);
        int roleA = va.isSource ? 0 : (va.isSink ? 2 : 1);
        int roleB = vb.isSource ? 0 : (vb.isSink ? 2 : 1);
        if (roleA != roleB) return roleA < roleB;
        return ea->y < eb->y;
    });

    bool linkedAny = false;
    for (size_t i = 0; i + 1 < sorted.size(); ++i) {
        const QString& srcEl = sorted[i];
        const QString& snkEl = sorted[i + 1];
        if (plan().wouldCycle(toStd(srcEl), toStd(snkEl))) continue;
        ElementView sv = view(srcEl);
        ElementView dv = view(snkEl);
        QString srcPad;
        for (const PadView& p : sv.srcs) {
            if (p.state != PadView::Linked) {
                srcPad = p.identity;
                break;
            }
        }
        QString sinkPad;
        for (const PadView& p : dv.sinks) {
            if (p.state != PadView::Linked) {
                sinkPad = p.templateName;
                break;
            }
        }
        if (sinkPad.isEmpty()) {
            if (const ElementInfo* inf = catalog().info(dv.factory)) {
                for (const PadTemplateInfo& t : inf->sinkTemplates()) {
                    if (t.isRequest()) {
                        sinkPad = fromStd(t.name);
                        break;
                    }
                }
            }
        }
        if (!srcPad.isEmpty() && !sinkPad.isEmpty()) {
            if (editGraph().linkExisting(toStd(srcEl), toStd(srcPad), toStd(snkEl), toStd(sinkPad))) {
                linkedAny = true;
                if (lastTouched) *lastTouched = snkEl;
            }
        }
    }
    return linkedAny;
}

bool Controller::connectSelectedElements(const QStringList& names) {
    if (editingBlocked() || names.size() < 2) return false;
    QStringList expanded = expandSelection(names);
    if (expanded.size() < 2) return false;

    return transact([&](gps::Graph&, Edit& e) {
        e.action = "Connected selected elements";
        if (!linkOrderedInternal(expanded, &e.touched)) return false;
        return true;
    });
}

bool Controller::disconnectBetweenElements(const QStringList& names) {
    if (names.size() < 2) return false;
    QStringList expanded = expandSelection(names);
    std::set<std::string> chosen;
    for (const QString& n : expanded) chosen.insert(toStd(n));
    return transact([&](gps::Graph& g, Edit& e) {
        e.action = "Disconnected links between selected elements";
        return g.disconnectBetween(chosen);
    });
}

bool Controller::niceAlign(const QStringList& elements) {
    const QStringList chosen = elements.isEmpty() ? QStringList() : expandSelection(elements);
    QStringList all;
    for (const gps::Element& e : plan().elements) all << fromStd(e.name);
    return layout_->align(chosen, layout_->pinOf(chosen.isEmpty() ? all : chosen));  // what is on screen stays there
}

// An edit of the user's, with automatic alignment on: the whole canvas follows, once for a burst of edits.
void Controller::alignAfterEdit() {
    if (!services_.view().autoAlign() || layout_->applying() || restoring_ || alignQueued_) return;
    alignQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        alignQueued_ = false;
        niceAlign();
    });
}

// An opened file has no positions: it is laid out from the top left corner once its first validation tells every pad.
void Controller::alignOpened() {
    if (std::exchange(alignWhenValidated_, false)) layout_->align();
}

gps::Removal Controller::previewCut(const QString& element, const QString& pad) const {
    return plan().previewCut(toStd(element), toStd(pad), catalog().traits());
}

gps::Removal Controller::previewReplace(const QString& element, const QString& pad) const {
    return plan().previewReplace(toStd(element), toStd(pad), catalog().traits());
}

gps::Removal Controller::previewDeleteSource(const QString& element) const {
    return plan().previewDeleteSource(toStd(element), catalog().traits());
}

// ---- update

void Controller::startUpdate() {
    execution_.supersede();
    updateTimer_.start();
    cancelled_ = false;
    updating_ = true;
    watch_->start();
    emit changed();
}

void Controller::dispatchUpdate() {
    cancelled_ = false;
    if (plan().empty() || plan().trackedSubplan(catalog().traits()).empty()) {
        updating_ = false; watch_->stop();
        pipeline_.observeIdle();
        emit changed();
        emit updateFinished(true);
        alignOpened();
        return;
    }
    updating_ = true;
    watch_->start();
    emit changed();
    backend_.setTimeout(updateTimeoutMs_);
    backend_.setSuspect(toStd(touched_));
    execution_.start([this](const gps::ExecutionResult& result) { finishUpdate(result); });
}

void Controller::cancelUpdate() {
    if (!updating_ || cancelling_) return;
    if (updateTimer_.isActive()) {
        updateTimer_.stop();
        execution_.supersede();
        updating_ = false; cancelling_ = false; cancelled_ = true;
        watch_->stop();
        emit changed(); emit updateFinished(false);
        return;
    }
    cancelling_ = true;
    log("Validation cancelled: stopping the validator");
    execution_.cancel();
    emit changed();
}

void Controller::waitMore() {
    watch_->extend();
    emit changed();
}

void Controller::finishUpdate(const gps::ExecutionResult& r) {
    updating_ = false;
    cancelling_ = false;
    watch_->stop();
    if (r.report.status == "cancelled") {
        cancelled_ = true;
        log("Validation cancelled — press Validate to run it again");
        emit changed();
        emit updateFinished(false);
        return;
    }
    log("Validation: " + statusText(result()) + (result().message.empty() ? QString() : " – " + fromStd(result().message)));
    emit changed();
    emit updateFinished(result().ok());
    alignOpened();
}

bool Controller::refresh() {
    if (busy() || plan().empty()) return false;
    note("Validated the pipeline again");
    startUpdate();
    return true;
}

// ---- files and runs

void Controller::newPlan() {
    if (busy()) return;
    pipeline_.reset();
    editingTemplates_.clear();
    knownModified_.clear();
    cancelled_ = false;
    path_.clear();
    libraryTemplate_.clear();
    dirty_ = false;
    emit changed();
}

void Controller::newProject(const QString& confPath) {
    newPlan();
    if (busy()) return;
    path_ = confPath;
    note("New project " + QFileInfo(confPath).completeBaseName());
    emit changed();
}

void Controller::open(const QString& path, bool imported) {
    load(path, imported ? QString() : path, imported, imported ? "Imported a gst-launch pipeline" : "Opened a plan file");
    if (!imported && QFile::exists(ProjectFile::pathForConf(path))) ProjectRegistry::remember(ProjectFile::pathForConf(path));
}

void Controller::restoreVersion(const QString& version) {
    load(version, path_, true, "Restored an earlier version");
}

// The plan of source becomes the document at documentPath ("" = untitled), once every factory it uses is known.
void Controller::load(const QString& source, const QString& documentPath, bool dirty, const QString& action, const QString& libraryTemplate) {
    if (busy()) return;
    auto loaded = std::make_shared<gps::Graph>();
    std::string error;
    // Its templates: the document's own copies (an earlier version is read from elsewhere), then the library.
    const std::vector<std::string> libraries{toStd(QFileInfo(documentPath).dir().filePath("templates")), toStd(TemplateLibrary::directory())};
    if (!gps::Graph::load(toStd(source), loaded.get(), &error, libraries)) {
        emit notice("Unable to open the file: " + fromStd(error));
        return;
    }
    QStringList factories;
    for (const gps::Element& e : loaded->elements) {
        if (!factories.contains(fromStd(e.factory))) factories << fromStd(e.factory);
    }
    opening_ = true;
    catalog().fetchInfos(factories, this, [this, loaded, documentPath, dirty, action, libraryTemplate](bool ok) {
        opening_ = false;
        if (!ok) {
            emit notice("Unable to open the file: the plan uses an element this installation does not have.");
            emit changed();
            return;
        }
        std::string failure;
        if (!pipeline_.reset(*loaded, &failure)) { emit notice(fromStd(failure)); return; }
        path_ = documentPath;
        dirty_ = dirty;
        libraryTemplate_ = libraryTemplate;
        note(action);
        alignWhenValidated_ = true;
        editingTemplates_.clear();
        checkTemplates(false);
        emit changed();
        startUpdate();
        offerNewerTemplates();
    });
}

bool Controller::importLaunch(const QString& description) {
    if (busy() || description.trimmed().isEmpty()) return false;
    const QString helper = findHelper("gps-import");
    if (helper.isEmpty()) { emit notice("gps-import was not found"); return false; }
    const QString path = tmp_.filePath("import.conf");
    opening_ = true;
    emit changed();
    HelperClient::run(this, "gps-import", {"--output", path, "--probe-ms", "1000", description.trimmed()}, 13000, logger(),
              [this, path](const HelperResult& result) {
        opening_ = false;
        gps::ImportReply reply;
        reply.status.clear();
        const bool answered = result.read(&reply);
        if (reply.status != "ok") {
            emit notice("Import failed: " + (answered ? fromStd(reply.message) : QString("gps-import did not answer")));
            emit changed();
            return;
        }
        log(QString("Imported native pipeline %1: %2 elements, %3 links").arg(fromStd(reply.nativeName)).arg(reply.elements).arg(reply.links));
        for (const std::string& warning : reply.warnings) log("[gps-import] " + fromStd(warning));
        if (!reply.warnings.empty()) emit notice("Imported with warnings. Review the Log and validate before running.");
        open(path, true);
    });
    return true;
}

bool Controller::save(const QString& path, QString* error) {
    QString target = path.isEmpty() ? path_ : path;
    if (!canSave() || target.isEmpty()) {
        if (error) *error = "Unable to save: " + (saveBlockedReason().isEmpty() ? QString("no file name") : saveBlockedReason());
        return false;
    }
    for (const QString& id : QSet<QString>(editingTemplates_)) commitTemplateEdit(id);  // saving ends every template edit
    if (!libraryTemplate_.isEmpty() && target == path_) {  // a library template: its next revision
        gps::Graph next = plan();
        next.revision = services_.library().nextRevision(libraryTemplate_, plan().revision);
        if (!services_.library().save(libraryTemplate_, next, error)) return false;
        transact([&](gps::Graph& g, Edit& edit) {
            g.revision = next.revision;
            edit.action = QString("Saved template %1, revision %2").arg(libraryTemplate_).arg(next.revision);
            edit.presentation = true;
            return true;
        });
        dirty_ = false;
        emit changed();
        return true;
    }
    libraryTemplate_.clear();  // saved elsewhere, it is a pipeline of its own
    gps::Graph saved = plan();
    saved.complete = result().complete();
    saved.name = toStd(QFileInfo(target).completeBaseName());
    // The file as it was stays (File > Version history).
    VersionHistory::keep(target, QByteArray::fromStdString(saved.toConf(gps::Graph::Form::Reference)));
    std::string failure;
    if (!saved.saveProject(toStd(target), &failure)) {
        if (error) *error = fromStd(failure);
        return false;
    }
    ProjectFile project = ProjectFile::load(ProjectFile::pathForConf(target));
    project.record(target, saved);
    if (project.save()) ProjectRegistry::remember(project.path());
    path_ = target;
    dirty_ = false;
    emit changed();
    return true;
}

// The complete plan in a temporary file for gps-run or gps-monitor; "" (and a notice) when it cannot be written.
QString Controller::runFile(const QString& name) {
    gps::Graph copy = plan();
    copy.complete = true;
    const QString file = tmp_.filePath(name);
    std::string error;
    if (copy.save(toStd(file), &error)) return file;
    emit notice("Unable to write the temporary plan file: " + fromStd(error));
    return QString();
}

bool Controller::run() {
    if (!canRun()) return false;
    const QString file = runFile("run.conf");
    if (file.isEmpty()) return false;
    running_ = true;
    log("Running…");
    runClient_ = HelperClient::run(this, "gps-run", {file}, 0, logger(), [this](const HelperResult& r) {
        running_ = false;
        runClient_ = nullptr;
        gps::RunResult answer;
        answer.status.clear();
        const bool answered = r.read(&answer);
        QString status;
        if (r.outcome == HelperResult::Finished && answered) status = fromStd(answer.status);
        else if (r.outcome == HelperResult::Crashed) status = "crashed";
        else if (r.outcome == HelperResult::NotFound) status = "not-found";
        else status = "error";
        QString message = answered ? fromStd(answer.message) : QString();
        log("Run finished: " + status + (message.isEmpty() ? "" : " – " + message));
        emit changed();
        emit runFinished(status);
    });
    emit changed();
    return true;
}

void Controller::stopRun() {
    if (runClient_) runClient_->interrupt();
}

bool Controller::startMonitoring() {
    if (!canRun()) return false;
    const QString helper = findHelper("gps-monitor");
    if (helper.isEmpty()) { emit notice("gps-monitor was not found"); return false; }
    const QString file = runFile("monitor.conf");
    if (file.isEmpty()) return false;
    monitorOutput_.clear();
    auto* client = new QProcess(this);
    monitorClient_ = client;
    monitoring_ = true;
    connect(client, &QProcess::readyReadStandardOutput, this, [this, client] {
        monitorOutput_ += client->readAllStandardOutput();
        int end = 0;
        while ((end = monitorOutput_.indexOf('\n')) >= 0) {
            QByteArray line = monitorOutput_.left(end);
            monitorOutput_.remove(0, end + 1);
            const std::string text = line.toStdString();
            gps::TelemetrySample sample;
            gps::RunResult finished;
            finished.status.clear();
            if (gps::fromJson(text, &sample)) emit telemetryReceived(sample);
            else if (gps::fromJson(text, &finished) && !finished.status.empty()) log("Monitor finished: " + fromStd(finished.status));
        }
    });
    connect(client, &QProcess::readyReadStandardError, this, [this, client] { log("[gps-monitor] " + QString::fromUtf8(client->readAllStandardError()).trimmed()); });
    connect(client, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { emit notice("Monitor could not start or stopped unexpectedly"); });
    connect(client, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, client](int, QProcess::ExitStatus) {
        if (monitorClient_ == client) { monitoring_ = false; monitorClient_ = nullptr; emit changed(); }
        client->deleteLater();
    });
    client->start(helper, {file});
    emit changed();
    return true;
}

void Controller::stopMonitoring() {
    if (!monitorClient_) return;
    auto* client = monitorClient_;
    if (client->state() == QProcess::Running) ::kill(static_cast<pid_t>(client->processId()), SIGINT);
    else client->terminate();
    QTimer::singleShot(2000, client, [client] { if (client->state() != QProcess::NotRunning) client->kill(); });
}
