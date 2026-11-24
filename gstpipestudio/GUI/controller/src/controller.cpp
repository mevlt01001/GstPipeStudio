// controller.cpp – see controller.h. Rules come from gps_pipeline; this file only sequences them with the helpers.
#include "controller.h"

#include "capstext.h"
#include "actions.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRectF>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <memory>
#include <set>
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

Controller::Controller(QObject* parent) : QObject(parent), watch_(new SlowWatch(this)), startup_(new StartupCheck(this)) {
    bool ok = false;
    int ms = qEnvironmentVariableIntValue("GPS_UPDATE_TIMEOUT_MS", &ok);
    if (ok && ms > 0) updateTimeoutMs_ = ms;
    loadCustomElements();
    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(35);
    connect(&updateTimer_, &QTimer::timeout, this, &Controller::dispatchUpdate);
    connect(&backend_, &HelperExecutionBackend::logLine, this, [this](const QString& line) { log("[gps-update] " + line); });
    connect(watch_, &SlowWatch::slow, this, [this] {
        log("Validation is taking longer than expected");
        emit changed();
    });
    connect(&catalog_, &FactoryCatalog::ready, this, [this] {
        loadCustomElements();
        emit changed();
    });
    connect(&catalog_, &FactoryCatalog::infoArrived, this, [this](const QString& factory) {
        pipeline_.registerFactory(toStd(factory), gps::definitionOf(*catalog_.info(factory)));
    });
    connect(&catalog_, &FactoryCatalog::notice, this, &Controller::notice);
    connect(&catalog_, &FactoryCatalog::logLine, this, logger());
    connect(startup_, &StartupCheck::finished, this, [this](const StartupResult& r) {
        log(r.state == StartupResult::Ready ? r.statusText() : "GStreamer check: the environment is not ready");
        if (r.state == StartupResult::Ready && !catalog_.loaded()) catalog_.load();
        emit environmentChecked();
        emit changed();
    });
}

void Controller::checkEnvironment() { startup_->start(); }

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
    if (!result().valid()) return "The pipeline is not validated yet — press Validate";
    if (!result().ok()) return "The last validation reported a problem (see the Validation tab)";
    return QString();
}

QString Controller::runBlockedReason() const {
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
    const StartupResult& env = startup_->result();
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

// ---- custom elements & groups

QString Controller::customElementsDir() {
    QString env = qEnvironmentVariable("GPS_CUSTOM_ELEMENTS_DIR");
    if (!env.isEmpty()) return env;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/custom_elements";
}

void Controller::loadCustomElements() {
    customElements_.clear();
    customPlans_.clear();
    QDir dir(customElementsDir());
    if (!dir.exists()) return;
    QStringList files = dir.entryList({"*.conf"}, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        gps::Graph sub;
        std::string err;
        if (!gps::Graph::load(toStd(dir.filePath(f)), &sub, &err) || sub.elements.empty()) continue;
        CustomElementInfo ci;
        ci.name = fromStd(sub.name).isEmpty() ? QFileInfo(f).completeBaseName() : fromStd(sub.name);
        ci.elementCount = static_cast<int>(sub.elements.size());
        QStringList factories;
        bool anyFreeSink = false;
        bool anyFreeSrc = false;
        for (const gps::Element& e : sub.elements) {
            QString fac = fromStd(e.factory);
            factories << fac;
            const FactoryEntry* entry = catalog_.entry(fac);
            bool isSrc = entry && entry->isSource, isSnk = entry && entry->isSink;
            if (!isSrc && sub.linksInto(e.name).empty()) anyFreeSink = true;
            if (!isSnk && sub.linksFromElement(e.name).empty()) anyFreeSrc = true;
        }
        ci.description = factories.join(" → ");
        ci.hasFreeSink = anyFreeSink;
        ci.hasFreeSrc = anyFreeSrc;
        ci.isSource = !anyFreeSink && anyFreeSrc;
        ci.isSink = anyFreeSink && !anyFreeSrc;
        customPlans_[ci.name] = sub;
        customElements_ << ci;
    }
}

const gps::Graph* Controller::customElementPlan(const QString& name) const {
    auto it = customPlans_.find(name);
    return it == customPlans_.end() ? nullptr : &it->second;
}

QString Controller::elementGroup(const QString& elementName) const {
    const gps::Element* e = plan().find(toStd(elementName));
    return e ? fromStd(e->group) : QString();
}

QStringList Controller::groupMembers(const QString& groupId) const {
    if (groupId.isEmpty()) return {};
    QStringList out;
    std::string g = toStd(groupId);
    for (const gps::Element& e : plan().elements) {
        if (e.group == g) out << fromStd(e.name);
    }
    return out;
}

QStringList Controller::expandSelectionWithGroups(const QStringList& elementNames) const {
    QStringList out;
    QSet<QString> seen;
    for (const QString& n : elementNames) {
        if (!plan().find(toStd(n))) continue;
        QString grp = elementGroup(n);
        if (!grp.isEmpty() && !isGroupExpanded(grp)) {
            for (const QString& m : groupMembers(grp)) {
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

void Controller::setGroupExpanded(const QString& groupId, bool expanded) {
    if (groupId.isEmpty()) return;
    if (expanded) expandedGroups_.insert(groupId);
    else expandedGroups_.remove(groupId);
    emit changed();
}

bool Controller::groupSelectedElements(const QStringList& elementNames, const QString& groupTitle) {
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty()) return false;
    QString cleanTitle = groupTitle.trimmed();
    if (cleanTitle.isEmpty()) cleanTitle = "CustomGroup";
    cleanTitle.replace('#', '_');
    return transact([&](gps::Graph& g, Edit& edit) {
        const std::string groupId = toStd(newGroupId(cleanTitle));
        for (const QString& n : expanded) {
            if (gps::Element* e = g.find(toStd(n))) e->group = groupId;
        }
        edit.action = "Grouped elements as " + cleanTitle;
        edit.presentation = true;
        return true;
    });
}

bool Controller::ungroupElements(const QString& groupId) {
    QStringList members = groupMembers(groupId);
    if (groupId.isEmpty() || members.isEmpty()) return false;
    return transact([&](gps::Graph& g, Edit& edit) {
        for (const QString& m : members) {
            if (gps::Element* e = g.find(toStd(m))) e->group.clear();
        }
        expandedGroups_.remove(groupId);
        edit.action = "Ungrouped " + groupId.section('#', 0, 0);
        edit.presentation = true;
        return true;
    });
}

bool Controller::saveCustomElement(const QString& customName, const QStringList& elementNames, const QString& description) {
    Q_UNUSED(description);
    QString clean = customName.trimmed();
    clean.replace('#', '_');
    if (clean.isEmpty()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty()) return false;

    gps::Graph sub = copySubplan(expanded);
    if (sub.elements.empty()) return false;
    sub.name = toStd(clean);

    // Normalize relative positions so top-left starts at (0, 0).
    double minX = sub.elements.front().x;
    double minY = sub.elements.front().y;
    for (const gps::Element& e : sub.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }
    for (gps::Element& e : sub.elements) {
        e.x -= minX;
        e.y -= minY;
        e.group = toStd(clean + "#0");
    }

    QDir dir(customElementsDir());
    if (!dir.exists() && !dir.mkpath(".")) return false;
    QString safeFile = clean;
    safeFile.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    std::string err;
    if (!sub.save(toStd(dir.filePath(safeFile + ".conf")), &err)) {
        emit notice("Unable to save custom element: " + fromStd(err));
        return false;
    }

    // Also group the selected elements on the active canvas as a collapsed instance of this custom element.
    if (!editingBlocked()) {
        groupSelectedElements(expanded, clean);
    }
    loadCustomElements();
    emit changed();
    return true;
}

bool Controller::removeCustomElement(const QString& customName) {
    QDir dir(customElementsDir());
    QString safeFile = customName;
    safeFile.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    bool removed = QFile::remove(dir.filePath(safeFile + ".conf"));
    loadCustomElements();
    emit changed();
    return removed;
}

QStringList Controller::addCustomElement(const QString& customName, double x, double y) {
    const gps::Graph* tmpl = customElementPlan(customName);
    if (!tmpl || editingBlocked()) return {};
    gps::Graph copy = *tmpl;
    for (gps::Element& e : copy.elements) e.group = toStd(customName);  // the paste gives the group a fresh id
    QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : freeSpot();
    return pasteSubplan(copy, at.x(), at.y());
}

QStringList Controller::addCustomElementAfter(const QString& srcElement, const QString& srcPad, const QString& customName) {
    const gps::Graph* tmpl = customElementPlan(customName);
    const gps::Element* from = plan().find(toStd(srcElement));
    if (!tmpl || !from || editingBlocked()) return {};
    if (!infosReady(*tmpl, [=] { addCustomElementAfter(srcElement, srcPad, customName); })) return {};
    const QString groupId = newGroupId(customName);
    QStringList created;
    transact([&](gps::Graph& g, Edit& edit) {
        const QPointF to(from->x + metrics::kColumn, from->y);
        for (const auto& entry : insertCopy(g, *tmpl, to - topLeft(*tmpl), groupId)) created << fromStd(entry.second);
        // srcElement.srcPad feeds the first free sink pad of the new group
        for (const PadView& p : groupView(groupId).sinks) {
            if (p.state != PadView::Linked) {
                g.linkExisting(toStd(srcElement), toStd(srcPad), toStd(p.element), toStd(p.templateName));
                break;
            }
        }
        edit.action = "Added custom element " + customName + " after " + srcElement + "." + srcPad;
        edit.touched = created.value(0);
        edit.validate = anyTracked(created);
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

ElementView Controller::view(const QString& name) const {
    ElementView v;
    v.name = name;
    const gps::Element* element = plan().find(toStd(name));
    if (!element) return v;

    v.factory = fromStd(element->factory);
    v.groupId = fromStd(element->group);
    if (!v.groupId.isEmpty()) v.groupMembers = groupMembers(v.groupId);
    if (const FactoryEntry* e = catalog_.entry(v.factory)) v.nvidia = e->nvidia;

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

    const ElementInfo* inf = catalog_.info(v.factory);
    if (!inf) return v;
    v.isSource = inf->entry.isSource;
    v.isSink = inf->entry.isSink;
    v.isTracked = plan().isElementTracked(element->name, catalog_.traits());
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

ElementView Controller::groupView(const QString& groupId) const {
    ElementView v;
    QStringList members = groupMembers(groupId);
    if (members.isEmpty()) return v;

    QString title = groupId.section('#', 0, 0);
    v.name = members.first();
    v.displayTitle = title.isEmpty() ? groupId : title;
    v.groupId = groupId;
    v.isCustomGroup = true;
    v.groupMembers = members;
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
            v.note = m + ": " + mv.note;
        } else if (v.note.isEmpty() && !mv.note.isEmpty() && !mv.isInfo) {
            v.note = m + ": " + mv.note;
        }
        for (const QString& pl : mv.propLines) {
            if (!pl.startsWith("…")) propSummary << (m + "." + pl);
        }
    }
    subgraphExternalPads(members, &v.sinks, &v.srcs);
    v.isSource = v.sinks.isEmpty() && !v.srcs.isEmpty();
    v.isSink = !v.sinks.isEmpty() && v.srcs.isEmpty();
    v.factory = QString("custom (%1) · %2").arg(members.size()).arg(factories.join(" → "));
    v.propLines = limitedLines(propSummary);
    if (!v.isTracked && v.note.isEmpty() && !v.isSource) {
        v.isInfo = true;
        v.note = "ℹ Unlinked custom element — not tracked until fed by a source.";
    }
    return v;
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
    const ElementInfo* inf = sink ? catalog_.info(fromStd(sink->factory)) : nullptr;
    return inf ? inf->padTemplate(l.sinkPad, "sink") : nullptr;
}

QList<ExistingTarget> Controller::existingTargets(const QString& element, const QString& identity) const {
    QList<ExistingTarget> out;
    std::vector<StructureInfo> allowed = padStructures(element, identity);
    for (const gps::Element& e : plan().elements) {
        const ElementInfo* inf = catalog_.info(fromStd(e.factory));
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

    // Exposed parameters of this element: drop the ones no longer ticked, keep the names the others have,
    // give new ones a unique name. Order stays as it was.
    auto isExposed = [&](const std::vector<gps::ExposedParam>& list, const std::string& property) {
        return std::any_of(list.begin(), list.end(), [&](const gps::ExposedParam& p) { return p.element == element && p.property == property; });
    };
    std::vector<gps::ExposedParam> all;
    for (const gps::ExposedParam& p : plan().exposed) {
        bool wanted = std::find(edit.exposed.begin(), edit.exposed.end(), p.property) != edit.exposed.end();
        if (p.element != element || wanted) all.push_back(p);
    }
    for (const std::string& property : edit.exposed) {
        if (isExposed(all, property)) continue;
        std::string name = property;
        auto taken = [&](const std::string& n) {
            return std::any_of(all.begin(), all.end(), [&](const gps::ExposedParam& p) { return p.name == n; });
        };
        for (int n = 2; taken(name); ++n) name = property + "_" + std::to_string(n);
        all.push_back({name, element, property});
    }
    editGraph().setExposed(all);
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
        return true;
    }
    edited(edit.touched, edit.validate.value_or(anyTracked({edit.touched})));
    return true;
}

bool Controller::commitTransaction() {
    if (!pipeline_.editor().editing()) return true;
    std::string error;
    const bool committed = pipeline_.editor().commit(&error);
    if (!error.empty()) emit notice(fromStd(error));
    return committed || error.empty();
}

void Controller::edited(const QString& touchedElement, bool validate) {
    if (!commitTransaction()) return;
    dirty_ = true;
    touched_ = touchedElement;
    emit changed();
    if (validate || updating_) {
        startUpdate();
    } else {
        emit updateFinished(true);
    }
}

bool Controller::undo() {
    if (!canUndo() || !pipeline_.editor().undo()) return false;
    note("Undo");
    edited(QString(), pipeline_.lastChange().kind == gps::ChangeKind::Execution);
    return true;
}

bool Controller::redo() {
    if (!canRedo() || !pipeline_.editor().redo()) return false;
    note("Redo");
    edited(QString(), pipeline_.lastChange().kind == gps::ChangeKind::Execution);
    return true;
}

gps::Graph Controller::copySubplan(const QStringList& elementNames) const {
    std::vector<std::string> ids;
    for (const auto& name : expandSelectionWithGroups(elementNames)) ids.push_back(toStd(name));
    return gps::GraphFragment(std::move(ids)).copy(plan());
}

QStringList Controller::pasteSubplan(const gps::Graph& clip, double targetX, double targetY) {
    if (editingBlocked() || clip.elements.empty()) return {};
    if (!infosReady(clip, [=] { pasteSubplan(clip, targetX, targetY); })) return {};
    const QPointF at = std::isfinite(targetX) ? QPointF(targetX, targetY) : topLeft(clip) + QPointF(40, 40);
    QStringList created;
    transact([&](gps::Graph& g, Edit& edit) {
        for (const auto& entry : insertCopy(g, clip, at - topLeft(clip))) created << fromStd(entry.second);
        edit.action = QString("Pasted %1 element(s)").arg(created.size());
        edit.touched = created.last();
        edit.validate = anyTracked(created);
        return true;
    });
    return created;
}

// The clip's factories are known (true), or are asked for and retry runs once they are (false).
bool Controller::infosReady(const gps::Graph& clip, std::function<void()> retry) {
    QStringList missing;
    for (const gps::Element& e : clip.elements) {
        QString factory = fromStd(e.factory);
        if (!catalog_.info(factory) && !missing.contains(factory)) missing << factory;
    }
    if (missing.isEmpty()) return true;
    catalog_.fetchInfos(missing, [retry](bool ok) {
        if (ok) retry();
    });
    return false;
}

QPointF Controller::topLeft(const gps::Graph& clip) {
    QPointF corner(clip.elements.front().x, clip.elements.front().y);
    for (const gps::Element& e : clip.elements) corner = QPointF(std::min(corner.x(), e.x), std::min(corner.y(), e.y));
    return corner;
}

// The clip's elements, links and exposed properties as new elements of the draft, moved by offset. Each group of the
// clip gets a fresh id, unless group names the one every element joins. Returns old name -> new name.
std::map<std::string, std::string> Controller::insertCopy(gps::Graph& g, const gps::Graph& clip, QPointF offset, const QString& group) {
    std::map<std::string, std::string> names, groups;
    for (const gps::Element& e : clip.elements) {
        const std::string created = g.addElement(e.factory, e.props, e.x + offset.x(), e.y + offset.y());
        std::string joined = toStd(group);
        if (joined.empty() && !e.group.empty()) {
            auto found = groups.find(e.group);
            if (found == groups.end()) found = groups.emplace(e.group, toStd(newGroupId(fromStd(e.group).section('#', 0, 0)))).first;
            joined = found->second;
        }
        g.find(created)->group = joined;
        ElementEdit edit{e.props, e.requestSrc, e.requestSink, {}};
        for (const gps::ExposedParam& p : clip.exposed) {
            if (p.element == e.name) edit.exposed.push_back(p.property);
        }
        applyEdit(created, edit);
        names[e.name] = created;
    }
    for (const gps::Connection& l : clip.links) {
        auto src = names.find(l.srcElement), sink = names.find(l.sinkElement);
        if (src != names.end() && sink != names.end()) g.linkExisting(src->second, l.srcPad, sink->second, l.sinkPad, l.caps, l.sinkPadProps);
    }
    return names;
}

QString Controller::newGroupId(const QString& title) {
    const QString id = title + "#" + QString::number(nextGroupId_++);
    expandedGroups_.remove(id);
    return id;
}

bool Controller::anyTracked(const QStringList& elements) const {
    return std::any_of(elements.begin(), elements.end(), [this](const QString& e) {
        return plan().isElementTracked(toStd(e), catalog_.traits());
    });
}

QStringList Controller::duplicateElements(const QStringList& names) {
    if (editingBlocked() || names.isEmpty()) return {};
    gps::Graph sub = copySubplan(names);
    if (sub.elements.empty()) return {};
    return pasteSubplan(sub);
}

bool Controller::addSource(const QString& factory, const ElementEdit& edit) {
    return addElement(factory, edit);
}

bool Controller::addElement(const QString& factory, const ElementEdit& edit, double x, double y) {
    if (!catalog_.info(factory)) return false;
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
    if (!catalog_.info(factory) || !from) return false;
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
        e.validate = anyTracked({element, sinkElement});
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
        e = {"Changed the caps of " + element + "." + pad, padText(element, pad), element, true};
        return true;
    });
}

bool Controller::cut(const QString& element, const QString& pad) {
    const QString linking = padText(element, pad);
    return transact([&](gps::Graph& g, Edit& e) {
        e = {"Cut the link at " + element + "." + pad, linking, element, true};
        return g.cut(toStd(element), toStd(pad), catalog_.traits()).links > 0;
    });
}

bool Controller::disconnectLink(const QString& element, const QString& pad) {
    if (editingBlocked()) return false;
    const bool wasTracked = plan().isElementTracked(toStd(element), catalog_.traits());
    const QString linking = padText(element, pad);
    gps::DisconnectAction action(toStd(element), toStd(pad));
    if (!pipeline_.editor().execute(action)) return false;
    note("Disconnected the link at " + element + "." + pad, linking);
    edited(element, wasTracked);
    return true;
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (!catalog_.info(factory)) return false;
    const QString linking = padText(element, pad);
    return transact([&](gps::Graph& g, Edit& e) {
        gps::Addition added = g.replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, catalog_.traits());
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
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty() && links.isEmpty()) return false;
    QStringList touched = expanded;
    for (const auto& link : links) touched << link.first;
    const bool tracked = anyTracked(touched);

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
        e.validate = tracked;
        return changedAny;
    });
}

bool Controller::spliceElement(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad) {
    return spliceElements({element}, linkSrcElement, linkSrcPad);
}

bool Controller::spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad) {
    if (editingBlocked() || elements.isEmpty()) return false;
    QStringList expanded = expandSelectionWithGroups(elements);
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
        if (const ElementInfo* inf = catalog_.info(view(only).factory)) {
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
    edited(expanded.first(), plan().isElementTracked(toStd(linkSrcElement), catalog_.traits()));
    return true;
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (!catalog_.info(fromStd(gps::kQueueFactory))) return false;
    return transact([&](gps::Graph& g, Edit& e) {
        std::string name = g.insertQueue(toStd(element), toStd(pad));
        if (name.empty()) return false;
        makeRoomForQueue(toStd(element), name);
        e = {"Inserted a queue after " + element + "." + pad, padText(element, pad), fromStd(name), std::nullopt};
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
    const bool tracked = anyTracked({queue});
    return transact([&](gps::Graph& g, Edit& e) {
        e.action = "Removed queue " + queue;
        e.validate = tracked;
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
        const QString group = fromStd(e->group);
        if (!group.isEmpty() && !isGroupExpanded(group)) {
            if (handledGroups.contains(group)) continue;
            handledGroups.insert(group);
            for (const auto& member : groupMembers(group)) {
                const auto* other = plan().find(toStd(member));
                updates[other->name] = {other->name, other->x + dx, other->y + dy};
            }
        } else updates[e->name] = {e->name, pt.x(), pt.y()};
    }
    layout::Placements moves;
    for (const auto& entry : updates) moves.append({fromStd(entry.first), QPointF(entry.second.x, entry.second.y)});
    move(moves);
}

bool Controller::move(const layout::Placements& placements) {
    std::vector<gps::Placement> moves;
    for (const auto& [element, at] : placements) moves.push_back({toStd(element), at.x(), at.y()});
    if (moves.empty() || !pipeline_.editor().execute(gps::MoveElementsAction(std::move(moves)))) return false;
    dirty_ = true;
    emit changed();
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
            if (const ElementInfo* inf = catalog_.info(dv.factory)) {
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
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;

    return transact([&](gps::Graph&, Edit& e) {
        e.action = "Connected selected elements";
        if (!linkOrderedInternal(expanded, &e.touched)) return false;
        e.validate = anyTracked(expanded);
        return true;
    });
}

bool Controller::disconnectBetweenElements(const QStringList& names) {
    if (names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    std::set<std::string> chosen;
    for (const QString& n : expanded) chosen.insert(toStd(n));
    const bool tracked = anyTracked(expanded);
    return transact([&](gps::Graph& g, Edit& e) {
        e.action = "Disconnected links between selected elements";
        e.validate = tracked;
        return g.disconnectBetween(chosen);
    });
}

QList<layout::Box> Controller::boxes(const QStringList& names) const {
    QList<layout::Box> out;
    for (const QString& name : names) {
        if (const gps::Element* e = plan().find(toStd(name))) out.append({name, QPointF(e->x, e->y), view(name).height()});
    }
    return out;
}

bool Controller::alignElements(const QStringList& names, Qt::Orientation orientation) {
    if (editingBlocked()) return false;
    QList<layout::Box> selected = boxes(expandSelectionWithGroups(names));
    if (selected.size() < 2 || !move(layout::align(selected, orientation))) return false;
    note(orientation == Qt::Horizontal ? "Aligned selected elements horizontally" : "Aligned selected elements vertically");
    return true;
}

bool Controller::arrangePipeline() {
    if (editingBlocked() || plan().elements.empty()) return false;
    const std::map<std::string, int> levels = plan().levels();
    if (levels.empty()) return false;
    QStringList names;
    for (const gps::Element& e : plan().elements) names << fromStd(e.name);
    QList<QPair<int, QString>> labels;
    for (const LinkView& link : linkViews()) labels.append({levels.at(toStd(link.srcElement)), link.negotiatedCaps});
    setPositions(layout::arrange(levels, boxes(names), labels));
    note("Arranged pipeline for readable caps and branches");
    return true;
}

gps::Removal Controller::previewCut(const QString& element, const QString& pad) const {
    return plan().previewCut(toStd(element), toStd(pad), catalog_.traits());
}

gps::Removal Controller::previewReplace(const QString& element, const QString& pad) const {
    return plan().previewReplace(toStd(element), toStd(pad), catalog_.traits());
}

gps::Removal Controller::previewDeleteSource(const QString& element) const {
    return plan().previewDeleteSource(toStd(element), catalog_.traits());
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
    if (plan().empty() || plan().trackedSubplan(catalog_.traits()).empty()) {
        updating_ = false; watch_->stop();
        pipeline_.observeIdle();
        emit changed();
        emit updateFinished(true);
        return;
    }
    updating_ = true;
    watch_->start();
    emit changed();
    backend_.setTimeout(updateTimeoutMs_);
    backend_.setSuspect(toStd(touched_));
    execution_.start(catalog_.traits(), [this](const gps::ExecutionResult& result) { finishUpdate(result); });
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
    cancelled_ = false;
    path_.clear();
    dirty_ = false;
    emit changed();
}

void Controller::open(const QString& path, bool imported) {
    if (busy()) return;
    auto loaded = std::make_shared<gps::Graph>();
    std::string error;
    if (!gps::Graph::load(toStd(path), loaded.get(), &error)) {
        emit notice("Unable to open the file: " + fromStd(error));
        return;
    }
    QStringList factories;
    for (const gps::Element& e : loaded->elements) {
        if (!factories.contains(fromStd(e.factory))) factories << fromStd(e.factory);
    }
    opening_ = true;
    catalog_.fetchInfos(factories, [this, loaded, path, imported](bool ok) {
        opening_ = false;
        if (!ok) {
            emit notice("Unable to open the file: the plan uses an element this installation does not have.");
            emit changed();
            return;
        }
        std::string failure;
        if (!pipeline_.reset(*loaded, &failure)) { emit notice(fromStd(failure)); return; }
        path_ = imported ? QString() : path;
        dirty_ = imported;
        note(imported ? "Imported a gst-launch pipeline" : "Opened a plan file");
        emit changed();
        if (imported) arrangePipeline();
        startUpdate();
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
    gps::Graph saved = plan();
    saved.complete = result().complete();
    saved.name = toStd(QFileInfo(target).completeBaseName());
    std::string failure;
    if (!saved.save(toStd(target), &failure)) {
        if (error) *error = fromStd(failure);
        return false;
    }
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
