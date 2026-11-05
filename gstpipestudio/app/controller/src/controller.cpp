// controller.cpp – see controller.h. Rules come from gps_plan; this file only sequences them with the helpers.
#include "controller.h"

#include "capsparse.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRectF>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <memory>
#include <set>

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
    size_t pinned = parseCapsChoice(caps).fields.size();
    return pinned == 0 ? QString("caps") : QString("caps · %1").arg(pinned);
}

}  // namespace

Controller::Controller(QObject* parent) : QObject(parent), watch_(new SlowWatch(this)), startup_(new StartupCheck(this)) {
    bool ok = false;
    int ms = qEnvironmentVariableIntValue("GPS_UPDATE_TIMEOUT_MS", &ok);
    if (ok && ms > 0) updateTimeoutMs_ = ms;
    loadCustomElements();
    connect(watch_, &SlowWatch::slow, this, [this] {
        log("Validation is taking longer than expected");
        emit changed();
    });
    connect(startup_, &StartupCheck::finished, this, [this](const StartupResult& r) {
        log(r.state == StartupResult::Ready ? r.statusText() : "GStreamer check: the environment is not ready");
        if (r.state == StartupResult::Ready && !catalogLoaded_) loadCatalog();
        emit environmentChecked();
        emit changed();
    });
}

void Controller::checkEnvironment() { startup_->start(); }

// ---- state

bool Controller::canSave() const { return saveBlockedReason().isEmpty(); }

bool Controller::canRun() const { return runBlockedReason().isEmpty(); }

QString Controller::saveBlockedReason() const {
    if (plan_.empty()) return "The canvas is empty";
    if (updating_) return "Validation is running";
    if (running_) return "The pipeline is running";
    if (opening_) return "A file is being opened";
    if (cancelled_) return "Validation was cancelled — press Validate";
    if (!result_.valid()) return "The pipeline is not validated yet — press Validate";
    if (!result_.ok()) return "The last validation reported a problem (see the Validation tab)";
    return QString();
}

QString Controller::runBlockedReason() const {
    QString reason = saveBlockedReason();
    if (reason.isEmpty() && !result_.isComplete()) return "The pipeline is not complete: link every free src pad";
    return reason;
}

bool Controller::helperStopped() const { return result_.status == "crashed" || result_.status == "stopped"; }

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
    const gps::PlanElement* e = plan_.find(toStd(element));
    return element + "." + identity + (e ? " (" + fromStd(e->factory) + ")" : QString());
}

SupportInfo Controller::supportInfo(const QString& title) const {
    SupportInfo i = SupportReport::systemInfo();
    i.title = title;
    i.action = action_;
    i.linking = linking_;
    if (result_.valid()) i.validation = result_.statusText() + (result_.message.isEmpty() ? "" : " – " + result_.message);
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
    for (const gps::ExposedParam& p : plan_.exposed) text += " " + fromStd(p.name) + "=VALUE";
    return text;
}

// ---- helpers

HelperClient* Controller::runHelper(const QString& name, const QStringList& args, int killAfterMs,
                                    std::function<void(const HelperResult&)> done) {
    auto* client = new HelperClient(this);
    connect(client, &HelperClient::stderrLine, this, [this, name](const QString& line) { log("[" + name + "] " + line); });
    connect(client, &HelperClient::finished, this, [client, done](const HelperResult& r) {
        client->deleteLater();
        done(r);
    });
    client->start(findHelper(name), args, killAfterMs);
    return client;
}

// ---- catalog

void Controller::loadCatalog() {
    runHelper("gps-inspect", {"list"}, 30000, [this](const HelperResult& r) {
        if (!r.hasJson) {
            emit notice("Unable to read the element list: gps-inspect did not run or gave an unusable answer.");
            return;
        }
        catalog_.clear();
        for (const QJsonValue& v : r.json["elements"].toArray()) catalog_ << ListEntry::fromJson(v.toObject());
        catalogLoaded_ = true;
        loadCustomElements();
        log(QString("Element list: %1 factories").arg(catalog_.size()));
        emit catalogReady();
        emit changed();
    });
}

const ElementInfo* Controller::info(const QString& factory) const {
    auto found = infos_.find(factory);
    return found == infos_.end() ? nullptr : &found->second;
}

void Controller::fetchInfo(const QString& factory, InfoCallback done) {
    if (const ElementInfo* cached = info(factory)) {
        done(cached);
        return;
    }
    pendingInfo_[factory].append(done);
    if (pendingInfo_[factory].size() > 1) return;  // a call for this factory is already on its way

    runHelper("gps-inspect", {"element", factory}, 30000, [this, factory](const HelperResult& r) {
        if (r.hasJson && r.json["factory"].toString() == factory) {
            infos_[factory] = ElementInfo::fromJson(r.json);
        } else {
            emit notice(QString("Unable to read the information of element '%1'.").arg(factory));
        }
        QList<InfoCallback> waiting = pendingInfo_.take(factory);
        for (const InfoCallback& cb : waiting) cb(info(factory));
    });
}

void Controller::fetchInfos(const QStringList& factories, std::function<void(bool)> done) {
    auto remaining = std::make_shared<int>(factories.size());
    auto failed = std::make_shared<bool>(false);
    if (factories.isEmpty()) {
        done(true);
        return;
    }
    for (const QString& f : factories) {
        fetchInfo(f, [remaining, failed, done](const ElementInfo* i) {
            if (!i) *failed = true;
            if (--*remaining == 0) done(!*failed);
        });
    }
}

void Controller::fetchCompatible(const QString& caps, NamesCallback done) {
    auto cached = compatible_.constFind(caps);
    if (cached != compatible_.constEnd()) {
        done(cached.value());
        return;
    }
    QStringList args = {"compatible"};
    if (!caps.isEmpty()) args << "--caps" << caps;
    runHelper("gps-inspect", args, 30000, [this, caps, done](const HelperResult& r) {
        QStringList names;
        if (r.hasJson) {
            for (const QJsonValue& v : r.json["factories"].toArray()) names << v.toString();
            compatible_.insert(caps, names);
        }
        done(names);
    });
}

gps::TraitsFn Controller::traits() const {
    return [this](const std::string& factory) {
        gps::FactoryTraits t;
        QString name = fromStd(factory);
        if (const ElementInfo* i = info(name)) {
            t.isSource = i->isSource;
        } else {
            for (const ListEntry& e : catalog_) {
                if (e.factory == name) t.isSource = e.isSource;
            }
        }
        return t;
    };
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
        gps::Plan sub;
        std::string err;
        if (!gps::Plan::load(toStd(dir.filePath(f)), &sub, &err) || sub.elements.empty()) continue;
        CustomElementInfo ci;
        ci.name = fromStd(sub.name).isEmpty() ? QFileInfo(f).completeBaseName() : fromStd(sub.name);
        ci.elementCount = static_cast<int>(sub.elements.size());
        QStringList factories;
        bool anyFreeSink = false;
        bool anyFreeSrc = false;
        for (const gps::PlanElement& e : sub.elements) {
            QString fac = fromStd(e.factory);
            factories << fac;
            bool isSrc = false, isSnk = false;
            if (const ElementInfo* inf = info(fac)) {
                isSrc = inf->isSource;
                isSnk = inf->isSink;
            } else {
                for (const ListEntry& le : catalog_) {
                    if (le.factory == fac) {
                        isSrc = le.isSource;
                        isSnk = le.isSink;
                        break;
                    }
                }
            }
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

const gps::Plan* Controller::customElementPlan(const QString& name) const {
    auto it = customPlans_.find(name);
    return it == customPlans_.end() ? nullptr : &it->second;
}

QString Controller::elementGroup(const QString& elementName) const {
    const gps::PlanElement* e = plan_.find(toStd(elementName));
    return e ? fromStd(e->group) : QString();
}

QStringList Controller::groupMembers(const QString& groupId) const {
    if (groupId.isEmpty()) return {};
    QStringList out;
    std::string g = toStd(groupId);
    for (const gps::PlanElement& e : plan_.elements) {
        if (e.group == g) out << fromStd(e.name);
    }
    return out;
}

QStringList Controller::expandSelectionWithGroups(const QStringList& elementNames) const {
    QStringList out;
    QSet<QString> seen;
    for (const QString& n : elementNames) {
        if (!plan_.find(toStd(n))) continue;
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
    if (busy()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty()) return false;
    QString cleanTitle = groupTitle.trimmed();
    if (cleanTitle.isEmpty()) cleanTitle = "CustomGroup";
    cleanTitle.replace('#', '_');
    QString groupId = cleanTitle + "#" + QString::number(nextGroupId_++);
    pushUndo();
    for (const QString& n : expanded) {
        if (gps::PlanElement* e = plan_.find(toStd(n))) e->group = toStd(groupId);
    }
    expandedGroups_.remove(groupId);
    dirty_ = true;
    note("Grouped elements as " + cleanTitle);
    emit changed();
    return true;
}

bool Controller::ungroupElements(const QString& groupId) {
    if (busy() || groupId.isEmpty()) return false;
    QStringList members = groupMembers(groupId);
    if (members.isEmpty()) return false;
    pushUndo();
    for (const QString& m : members) {
        if (gps::PlanElement* e = plan_.find(toStd(m))) e->group.clear();
    }
    expandedGroups_.remove(groupId);
    dirty_ = true;
    note("Ungrouped " + groupId.section('#', 0, 0));
    emit changed();
    return true;
}

bool Controller::saveCustomElement(const QString& customName, const QStringList& elementNames, const QString& description) {
    Q_UNUSED(description);
    QString clean = customName.trimmed();
    clean.replace('#', '_');
    if (clean.isEmpty()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty()) return false;

    gps::Plan sub = copySubplan(expanded);
    if (sub.elements.empty()) return false;
    sub.name = toStd(clean);

    // Normalize relative positions so top-left starts at (0, 0).
    double minX = sub.elements.front().x;
    double minY = sub.elements.front().y;
    for (const gps::PlanElement& e : sub.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }
    for (gps::PlanElement& e : sub.elements) {
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
    if (!busy()) {
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
    const gps::Plan* tmpl = customElementPlan(customName);
    if (!tmpl || busy()) return {};
    gps::Plan copy = *tmpl;
    QString groupId = customName + "#" + QString::number(nextGroupId_++);
    for (gps::PlanElement& e : copy.elements) e.group = toStd(groupId);
    expandedGroups_.remove(groupId);
    QPointF at = (x >= 0.0 && y >= 0.0) ? QPointF(x, y) : freeSpot();
    return pasteSubplan(copy, at.x(), at.y());
}

QStringList Controller::addCustomElementAfter(const QString& srcElement, const QString& srcPad, const QString& customName) {
    const gps::Plan* tmpl = customElementPlan(customName);
    const gps::PlanElement* from = plan_.find(toStd(srcElement));
    if (!tmpl || !from || busy()) return {};
    gps::Plan copy = *tmpl;
    QString groupId = customName + "#" + QString::number(nextGroupId_++);
    for (gps::PlanElement& e : copy.elements) e.group = toStd(groupId);
    expandedGroups_.remove(groupId);

    // Ensure all factories are cached, then insert and link the entry element.
    QStringList missing;
    for (const gps::PlanElement& e : copy.elements) {
        QString fac = fromStd(e.factory);
        if (!info(fac) && !missing.contains(fac)) missing << fac;
    }
    if (!missing.isEmpty()) {
        fetchInfos(missing, [this, srcElement, srcPad, customName](bool ok) {
            if (ok) addCustomElementAfter(srcElement, srcPad, customName);
        });
        return {};
    }
    pushUndo();
    double baseX = from->x + metrics::kColumn;
    double baseY = from->y;
    double minX = copy.elements.front().x;
    double minY = copy.elements.front().y;
    for (const gps::PlanElement& e : copy.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }

    std::map<std::string, std::string> nameMap;
    QStringList createdNames;
    for (const gps::PlanElement& e : copy.elements) {
        std::string created = plan_.addElement(e.factory, e.props, baseX + (e.x - minX), baseY + (e.y - minY));
        if (gps::PlanElement* pe = plan_.find(created)) pe->group = toStd(groupId);
        ElementEdit edit;
        edit.props = e.props;
        edit.requestSrc = e.requestSrc;
        edit.requestSink = e.requestSink;
        applyEdit(created, edit);
        nameMap[e.name] = created;
        createdNames << fromStd(created);
    }
    for (const gps::PlanLink& l : copy.links) {
        auto itSrc = nameMap.find(l.srcElement);
        auto itSnk = nameMap.find(l.sinkElement);
        if (itSrc != nameMap.end() && itSnk != nameMap.end()) {
            plan_.linkExisting(itSrc->second, l.srcPad, itSnk->second, l.sinkPad, l.caps, l.sinkPadProps);
        }
    }

    // Link srcElement.srcPad to the first free sink pad of the custom element group.
    ElementView gv = groupView(groupId);
    for (const PadView& p : gv.sinks) {
        if (p.state != PadView::Linked) {
            plan_.linkExisting(toStd(srcElement), toStd(srcPad), toStd(p.element), toStd(p.templateName));
            break;
        }
    }
    note("Added custom element " + customName + " after " + srcElement + "." + srcPad);
    bool anyTracked = false;
    for (const QString& n : createdNames) {
        if (plan_.isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    edited(createdNames.value(0), anyTracked);
    return createdNames;
}

// ---- views

QString Controller::elementNote(const QString& element, bool* isError) const {
    *isError = false;
    if (result_.valid() && !result_.ok() && result_.errorElement == element) {
        *isError = true;
        return result_.message;
    }
    for (const QStringList* list : {&result_.refused, &result_.missing}) {
        for (const QString& entry : *list) {
            if (entry.startsWith(element + ".") || entry.startsWith(element + ":")) return entry;
        }
    }
    return QString();
}

ElementView Controller::view(const QString& name) const {
    ElementView v;
    v.name = name;
    const gps::PlanElement* element = plan_.find(toStd(name));
    if (!element) return v;

    v.factory = fromStd(element->factory);
    v.groupId = fromStd(element->group);
    if (!v.groupId.isEmpty()) v.groupMembers = groupMembers(v.groupId);
    for (const ListEntry& e : catalog_) {
        if (e.factory == v.factory) v.nvidia = e.nvidia;
    }

    // Changed properties, the exposed ones marked.
    QStringList lines;
    for (const auto& entry : element->props) {
        const auto& key = entry.first;
        const auto& value = entry.second;
        bool exposed = std::any_of(plan_.exposed.begin(), plan_.exposed.end(),
                                   [&](const gps::ExposedParam& p) { return p.element == element->name && p.property == key; });
        lines << (exposed ? "↗ " : "") + fromStd(key) + " = " + fromStd(value);
    }
    if (lines.size() > metrics::kMaxPropLines) {
        int hidden = lines.size() - (metrics::kMaxPropLines - 1);
        lines = lines.mid(0, metrics::kMaxPropLines - 1);
        lines << QString("… +%1 more").arg(hidden);
    }
    v.propLines = lines;
    v.note = elementNote(name, &v.isError);

    const ElementInfo* inf = info(v.factory);
    if (!inf) return v;
    v.isSource = inf->isSource;
    v.isSink = inf->isSink;
    v.isTracked = plan_.isElementTracked(element->name, traits());
    if (!v.isTracked && v.note.isEmpty()) {
        v.isInfo = true;
        v.note = "ℹ Unlinked sink pad — not tracked until fed by a source.";
    }

    // Sink pads: Always ones are fixed, a Request one exists per link or per requestSink count.
    for (const PadTemplateInfo& t : inf->padTemplates) {
        if (t.isSrc()) continue;
        QString templateMedia = t.structures.isEmpty() ? QString() : t.structures.first().title();
        auto make = [&](const QString& identity, int link, int k) {
            PadView p;
            p.element = name;
            p.identity = identity;
            p.templateName = t.name;
            p.isSrc = false;
            p.link = link;
            p.state = link >= 0 ? PadView::Linked : PadView::Empty;
            p.label = t.isRequest() ? fillTemplate(t.name, k) : t.name;
            p.media = templateMedia;
            p.tip = t.caps;
            int seen = 0;
            for (const PadReport& r : result_.pads) {
                if (r.element != name || r.isSrc || r.templateName != t.name || seen++ != k) continue;
                p.label = r.pad;
                if (!r.caps.isEmpty()) {
                    p.media = mediaOf(r.caps);
                    p.tip = r.caps;
                    p.caps = r.caps;
                }
            }
            return p;
        };
        if (!t.isRequest()) {
            int link = -1;
            for (size_t i = 0; i < plan_.links.size(); ++i) {
                if (plan_.links[i].sinkElement == element->name && plan_.links[i].sinkPad == toStd(t.name)) link = static_cast<int>(i);
            }
            v.sinks << make(t.name, link, 0);
        } else {
            int k = 0;
            for (size_t i = 0; i < plan_.links.size(); ++i) {
                if (plan_.links[i].sinkElement == element->name && plan_.links[i].sinkPad == toStd(t.name)) {
                    v.sinks << make(fromStd(gps::padIdentity(toStd(t.name), k)), static_cast<int>(i), k);
                    ++k;
                }
            }
            auto reqCount = element->requestSink.find(toStd(t.name));
            int totalWanted = reqCount == element->requestSink.end() ? k : std::max(k, reqCount->second);
            for (; k < totalWanted; ++k) {
                v.sinks << make(fromStd(gps::padIdentity(toStd(t.name), k)), -1, k);
            }
        }
    }

    // Src pads: Always from the template, Request from requestSrc, Sometimes from the last update and the plan.
    for (const PadTemplateInfo& t : inf->padTemplates) {
        if (!t.isSrc()) continue;
        std::map<int, QString> byIndex;  // k -> identity
        auto note = [&](const QString& identity) {
            std::string tmpl;
            int k = 0;
            if (gps::splitPadIdentity(toStd(identity), &tmpl, &k) && fromStd(tmpl) == t.name) byIndex[k] = identity;
        };
        for (const PadReport& r : result_.pads) {
            if (r.element == name && r.isSrc) note(r.identity);
        }
        for (const gps::PlanLink& l : plan_.links) {
            if (l.srcElement == element->name) note(fromStd(l.srcPad));
        }

        QStringList identities;
        if (t.presence == "always") {
            identities << t.name;
        } else if (t.isRequest()) {
            auto count = element->requestSrc.find(toStd(t.name));
            int n = count == element->requestSrc.end() ? 0 : count->second;
            if (!byIndex.empty()) n = std::max(n, byIndex.rbegin()->first + 1);
            for (int k = 0; k < n; ++k) identities << fromStd(gps::padIdentity(toStd(t.name), k));
        } else {
            for (const auto& entry : byIndex) identities << entry.second;
        }

        for (const QString& identity : identities) {
            PadView p;
            p.element = name;
            p.identity = identity;
            p.templateName = t.name;
            p.isSrc = true;
            std::string tmpl;
            int k = 0;
            gps::splitPadIdentity(toStd(identity), &tmpl, &k);
            p.label = t.presence == "always" ? t.name : fillTemplate(t.name, k);
            p.link = plan_.linkFrom(toStd(name), toStd(identity));
            const PadReport* r = result_.pad(name, identity, true);
            if (r) {
                p.label = r->pad.isEmpty() ? p.label : r->pad;
                QString caps = r->caps.isEmpty() ? r->allowed : r->caps;
                p.media = mediaOf(caps);
                p.tip = caps;
                p.caps = r->caps;
            }
            p.state = p.link >= 0 ? PadView::Linked : ((!v.isTracked || (r && r->unknownData)) ? PadView::Unknown : PadView::Free);
            v.srcs << p;
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
                bool internal = sp.link >= 0 && sp.link < static_cast<int>(plan_.links.size()) &&
                                memberSet.contains(fromStd(plan_.links[sp.link].srcElement));
                if (!internal) *outSinks << sp;
            }
        }
        if (outSrcs) {
            for (const PadView& srp : mv.srcs) {
                bool internal = srp.link >= 0 && srp.link < static_cast<int>(plan_.links.size()) &&
                                memberSet.contains(fromStd(plan_.links[srp.link].sinkElement));
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
    if (propSummary.size() > metrics::kMaxPropLines) {
        int hidden = propSummary.size() - (metrics::kMaxPropLines - 1);
        propSummary = propSummary.mid(0, metrics::kMaxPropLines - 1);
        propSummary << QString("… +%1 more").arg(hidden);
    }
    v.propLines = propSummary;
    if (!v.isTracked && v.note.isEmpty() && !v.isSource) {
        v.isInfo = true;
        v.note = "ℹ Unlinked custom element — not tracked until fed by a source.";
    }
    return v;
}

QString Controller::linkSinkIdentity(int link) const {
    const gps::PlanLink& l = plan_.links[link];
    if (!gps::isTemplateName(l.sinkPad)) return fromStd(l.sinkPad);
    int k = 0;
    for (int i = 0; i < link; ++i) {
        if (plan_.links[i].sinkElement == l.sinkElement && plan_.links[i].sinkPad == l.sinkPad) ++k;
    }
    return fromStd(gps::padIdentity(l.sinkPad, k));
}

QList<LinkView> Controller::linkViews() const {
    QList<LinkView> out;
    for (size_t i = 0; i < plan_.links.size(); ++i) {
        const gps::PlanLink& l = plan_.links[i];
        LinkView v;
        v.index = static_cast<int>(i);
        v.srcElement = fromStd(l.srcElement);
        v.srcIdentity = fromStd(l.srcPad);
        v.sinkElement = fromStd(l.sinkElement);
        v.sinkIdentity = linkSinkIdentity(v.index);
        v.badge = l.caps.empty() ? QString() : badgeOf(fromStd(l.caps));
        v.tip = fromStd(l.caps);
        const PadReport* srcRep = result_.pad(v.srcElement, v.srcIdentity, true);
        if (srcRep && !srcRep->caps.isEmpty()) {
            v.negotiatedCaps = srcRep->caps;
        } else if (!l.caps.empty()) {
            v.negotiatedCaps = fromStd(l.caps);
        }
        if (v.tip.isEmpty() && !v.negotiatedCaps.isEmpty()) v.tip = v.negotiatedCaps;
        out << v;
    }
    return out;
}

QList<StructureInfo> Controller::padStructures(const QString& element, const QString& identity) const {
    const PadReport* r = result_.pad(element, identity, true);
    return r ? r->allowedStructures : QList<StructureInfo>();
}

QString Controller::padAllowed(const QString& element, const QString& identity) const {
    const PadReport* r = result_.pad(element, identity, true);
    return r ? r->allowed : QString();
}

const PadTemplateInfo* Controller::linkSinkTemplate(int link) const {
    if (link < 0 || link >= static_cast<int>(plan_.links.size())) return nullptr;
    const gps::PlanLink& l = plan_.links[link];
    const gps::PlanElement* sink = plan_.find(l.sinkElement);
    const ElementInfo* inf = sink ? info(fromStd(sink->factory)) : nullptr;
    return inf ? inf->padTemplate(fromStd(l.sinkPad), "sink") : nullptr;
}

QList<ExistingTarget> Controller::existingTargets(const QString& element, const QString& identity) const {
    QList<ExistingTarget> out;
    QList<StructureInfo> allowed = padStructures(element, identity);
    for (const gps::PlanElement& e : plan_.elements) {
        const ElementInfo* inf = info(fromStd(e.factory));
        if (!inf || plan_.wouldCycle(toStd(element), e.name)) continue;
        for (const PadTemplateInfo& t : inf->sinkTemplates()) {
            if (!capsCompatible(allowed, t.structures)) continue;
            if (!t.isRequest() && plan_.sinkPadTaken(e.name, toStd(t.name))) continue;
            out.append({fromStd(e.name), fromStd(e.factory), t.name, t.isRequest()});
        }
    }
    return out;
}

QList<PadTemplateInfo> Controller::sinkChoices(const ElementInfo& inf, const QString& element, const QString& identity) const {
    return inf.sinkTemplatesFor(padStructures(element, identity));
}

// ---- layout

QPointF Controller::freeSpot() const {
    double y = 40;
    for (const gps::PlanElement& e : plan_.elements) y = std::max(y, e.y + view(fromStd(e.name)).height() + 40);
    return QPointF(40, y);
}

// Moves an element down until it touches no earlier one. Elements the user placed apart stay where they are.
void Controller::arrange() {
    std::vector<QRectF> placed;
    for (gps::PlanElement& e : plan_.elements) {
        QRectF rect(e.x, e.y, metrics::kBoxWidth, view(fromStd(e.name)).height());
        for (bool moved = true; moved;) {
            moved = false;
            for (const QRectF& other : placed) {
                if (rect.adjusted(-metrics::kGap / 2, -metrics::kGap / 2, metrics::kGap / 2, metrics::kGap / 2).intersects(other)) {
                    rect.moveTop(other.bottom() + metrics::kGap);
                    moved = true;
                }
            }
        }
        e.x = rect.x();
        e.y = rect.y();
        placed.push_back(rect);
    }
}

// ---- edits

void Controller::applyEdit(const std::string& element, const ElementEdit& edit) {
    gps::PlanElement* e = plan_.find(element);
    if (!e) return;

    std::set<std::string> templates;
    for (const auto& entry : e->requestSrc) templates.insert(entry.first);
    for (const auto& entry : edit.requestSrc) templates.insert(entry.first);
    for (const std::string& t : templates) {
        auto wanted = edit.requestSrc.find(t);
        int count = wanted == edit.requestSrc.end() ? 0 : wanted->second;
        if (!plan_.setRequestSrc(element, t, count)) emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
    }

    std::set<std::string> sinkTemplates;
    for (const auto& entry : e->requestSink) sinkTemplates.insert(entry.first);
    for (const auto& entry : edit.requestSink) sinkTemplates.insert(entry.first);
    for (const std::string& t : sinkTemplates) {
        auto wanted = edit.requestSink.find(t);
        int count = wanted == edit.requestSink.end() ? 0 : wanted->second;
        if (!plan_.setRequestSink(element, t, count)) emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
    }

    // Exposed parameters of this element: drop the ones no longer ticked, keep the names the others have,
    // give new ones a unique name. Order stays as it was.
    auto isExposed = [&](const std::vector<gps::ExposedParam>& list, const std::string& property) {
        return std::any_of(list.begin(), list.end(), [&](const gps::ExposedParam& p) { return p.element == element && p.property == property; });
    };
    std::vector<gps::ExposedParam> all;
    for (const gps::ExposedParam& p : plan_.exposed) {
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
    plan_.setExposed(all);
}

void Controller::pushUndo() {
    undoStack_.push_back(plan_);
    if (undoStack_.size() > 64) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void Controller::edited(const QString& touchedElement, bool validate) {
    dirty_ = true;
    touched_ = touchedElement;
    emit changed();
    if (validate) {
        startUpdate();
    } else {
        emit updateFinished(true);
    }
}

bool Controller::undo() {
    if (!canUndo()) return false;
    redoStack_.push_back(plan_);
    plan_ = undoStack_.back();
    undoStack_.pop_back();
    note("Undo");
    edited(QString(), true);
    return true;
}

bool Controller::redo() {
    if (!canRedo()) return false;
    undoStack_.push_back(plan_);
    plan_ = redoStack_.back();
    redoStack_.pop_back();
    note("Redo");
    edited(QString(), true);
    return true;
}

gps::Plan Controller::copySubplan(const QStringList& elementNames) const {
    gps::Plan sub;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    std::set<std::string> chosen;
    for (const QString& n : expanded) {
        std::string s = toStd(n);
        if (const gps::PlanElement* pe = plan_.find(s)) {
            chosen.insert(s);
            sub.elements.push_back(*pe);
        }
    }
    for (const gps::PlanLink& l : plan_.links) {
        if (chosen.count(l.srcElement) && chosen.count(l.sinkElement)) {
            sub.links.push_back(l);
        }
    }
    for (const gps::ExposedParam& ep : plan_.exposed) {
        if (chosen.count(ep.element)) {
            sub.exposed.push_back(ep);
        }
    }
    return sub;
}

QStringList Controller::pasteSubplan(const gps::Plan& clip, double targetX, double targetY) {
    if (busy() || clip.elements.empty()) return {};
    QStringList missing;
    for (const gps::PlanElement& e : clip.elements) {
        QString fac = fromStd(e.factory);
        if (!info(fac) && !missing.contains(fac)) missing << fac;
    }
    if (!missing.isEmpty()) {
        fetchInfos(missing, [this, clip, targetX, targetY](bool ok) {
            if (ok) pasteSubplan(clip, targetX, targetY);
        });
        return {};
    }

    double minX = clip.elements.front().x;
    double minY = clip.elements.front().y;
    for (const gps::PlanElement& e : clip.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }
    double offsetX = (targetX >= 0.0) ? (targetX - minX) : 40.0;
    double offsetY = (targetY >= 0.0) ? (targetY - minY) : 40.0;

    pushUndo();
    std::map<std::string, std::string> nameMap;
    std::map<std::string, std::string> groupMap;
    QStringList createdNames;
    bool anyTracked = false;

    for (const gps::PlanElement& e : clip.elements) {
        std::string created = plan_.addElement(e.factory, e.props, e.x + offsetX, e.y + offsetY);
        if (!e.group.empty()) {
            auto git = groupMap.find(e.group);
            if (git == groupMap.end()) {
                QString baseTitle = fromStd(e.group).section('#', 0, 0);
                std::string newGrp = toStd(baseTitle + "#" + QString::number(nextGroupId_++));
                git = groupMap.emplace(e.group, newGrp).first;
            }
            if (gps::PlanElement* pe = plan_.find(created)) pe->group = git->second;
        }
        ElementEdit edit;
        edit.props = e.props;
        edit.requestSrc = e.requestSrc;
        edit.requestSink = e.requestSink;
        for (const gps::ExposedParam& ep : clip.exposed) {
            if (ep.element == e.name) edit.exposed.push_back(ep.property);
        }
        applyEdit(created, edit);
        nameMap[e.name] = created;
        createdNames << fromStd(created);
    }

    for (const gps::PlanLink& l : clip.links) {
        auto itSrc = nameMap.find(l.srcElement);
        auto itSnk = nameMap.find(l.sinkElement);
        if (itSrc != nameMap.end() && itSnk != nameMap.end()) {
            plan_.linkExisting(itSrc->second, l.srcPad, itSnk->second, l.sinkPad, l.caps, l.sinkPadProps);
        }
    }

    for (const QString& n : createdNames) {
        if (plan_.isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    if (!createdNames.isEmpty()) {
        note(QString("Pasted %1 element(s)").arg(createdNames.size()));
        edited(createdNames.last(), anyTracked);
    }
    return createdNames;
}

QStringList Controller::duplicateElements(const QStringList& names) {
    if (busy() || names.isEmpty()) return {};
    gps::Plan sub = copySubplan(names);
    if (sub.elements.empty()) return {};
    return pasteSubplan(sub, -1.0, -1.0);
}

bool Controller::addSource(const QString& factory, const ElementEdit& edit) {
    return addElement(factory, edit);
}

bool Controller::addElement(const QString& factory, const ElementEdit& edit, double x, double y) {
    if (busy() || !info(factory)) return false;
    pushUndo();
    QPointF at = (x >= 0.0 && y >= 0.0) ? QPointF(x, y) : freeSpot();
    std::string name = plan_.addElement(toStd(factory), edit.props, at.x(), at.y());
    note("Added " + factory);
    applyEdit(name, edit);
    bool isTracked = plan_.isElementTracked(name, traits());
    edited(fromStd(name), isTracked);
    return true;
}

bool Controller::addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                          const ElementEdit& edit) {
    if (busy() || !info(factory)) return false;
    const gps::PlanElement* from = plan_.find(toStd(element));
    if (!from) return false;
    pushUndo();
    std::string name = plan_.addAfter(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props,
                                      from->x + metrics::kColumn, from->y);
    if (name.empty()) {
        undoStack_.pop_back();
        return false;
    }
    applyEdit(name, edit);
    note("Added " + factory + " after " + element + "." + pad, padText(element, pad) + " -> " + padText(fromStd(name), sinkPad));
    bool isTracked = plan_.isElementTracked(name, traits());
    edited(fromStd(name), isTracked);
    return true;
}

bool Controller::linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad) {
    if (busy()) return false;
    if (element == sinkElement || plan_.wouldCycle(toStd(element), toStd(sinkElement))) {
        emit notice(fromStd(gps::Plan::ouroborosMessage(toStd(element), toStd(sinkElement))));
        return false;
    }
    pushUndo();
    if (!plan_.linkExisting(toStd(element), toStd(pad), toStd(sinkElement), toStd(sinkPad))) {
        undoStack_.pop_back();
        return false;
    }
    note("Linked " + element + "." + pad + " to an existing element", padText(element, pad) + " -> " + padText(sinkElement, sinkPad));
    bool isTracked = plan_.isElementTracked(toStd(element), traits()) || plan_.isElementTracked(toStd(sinkElement), traits());
    edited(sinkElement, isTracked);
    return true;
}

bool Controller::editElement(const QString& element, const ElementEdit& edit) {
    if (busy() || !plan_.find(toStd(element))) return false;
    pushUndo();
    plan_.setProps(toStd(element), edit.props);
    applyEdit(toStd(element), edit);
    const gps::PlanElement* e = plan_.find(toStd(element));
    note("Edited the properties of " + element + (e ? " (" + fromStd(e->factory) + ")" : QString()));
    bool isTracked = plan_.isElementTracked(toStd(element), traits());
    edited(element, isTracked);
    return true;
}

bool Controller::setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps) {
    if (busy() || plan_.linkFrom(toStd(element), toStd(pad)) < 0) return false;
    pushUndo();
    plan_.setCaps(toStd(element), toStd(pad), toStd(caps));
    plan_.setSinkPadProps(toStd(element), toStd(pad), sinkPadProps);
    note("Changed the caps of " + element + "." + pad, padText(element, pad));
    edited(element, true);
    return true;
}

bool Controller::cut(const QString& element, const QString& pad) {
    QString linking = padText(element, pad);
    if (busy()) return false;
    pushUndo();
    if (plan_.cut(toStd(element), toStd(pad), traits()).links == 0) {
        undoStack_.pop_back();
        return false;
    }
    note("Cut the link at " + element + "." + pad, linking);
    edited(element, true);
    return true;
}

bool Controller::disconnectLink(const QString& element, const QString& pad) {
    QString linking = padText(element, pad);
    if (busy()) return false;
    bool wasTracked = plan_.isElementTracked(toStd(element), traits());
    pushUndo();
    if (!plan_.disconnectLink(toStd(element), toStd(pad))) {
        undoStack_.pop_back();
        return false;
    }
    note("Disconnected the link at " + element + "." + pad, linking);
    edited(element, wasTracked);
    return true;
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (busy() || !info(factory)) return false;
    QString linking = padText(element, pad);
    pushUndo();
    gps::Addition added = plan_.replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, traits());
    if (added.element.empty()) {
        undoStack_.pop_back();
        return false;
    }
    note("Replaced the element behind " + element + "." + pad + " with " + factory, linking + " -> " + factory);
    applyEdit(added.element, edit);
    edited(fromStd(added.element), plan_.isElementTracked(added.element, traits()));
    return true;
}

bool Controller::deleteSource(const QString& element) {
    return deleteSelection({element}, {}, false);
}

bool Controller::deleteElement(const QString& element, bool autoHeal) {
    return deleteSelection({element}, {}, autoHeal);
}

bool Controller::deleteSelection(const QStringList& elementNames, const QList<QPair<QString, QString>>& links, bool autoHeal) {
    if (busy()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty() && links.isEmpty()) return false;

    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan_.isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    for (const auto& [srcEl, srcPad] : links) {
        Q_UNUSED(srcPad);
        if (plan_.isElementTracked(toStd(srcEl), traits())) anyTracked = true;
    }

    int exitTotalSrcPads = 1;
    QList<PadView> extSrcs;
    subgraphExternalPads(expanded, nullptr, &extSrcs);
    if (!extSrcs.isEmpty()) exitTotalSrcPads = extSrcs.size();

    std::vector<std::string> stdNames;
    for (const QString& n : expanded) stdNames.push_back(toStd(n));

    pushUndo();
    bool changedAny = false;
    for (const auto& [srcEl, srcPad] : links) {
        if (plan_.disconnectLink(toStd(srcEl), toStd(srcPad))) changedAny = true;
    }
    if (!stdNames.empty()) {
        if (plan_.deleteElements(stdNames, autoHeal, exitTotalSrcPads)) changedAny = true;
    }
    if (!changedAny) {
        undoStack_.pop_back();
        return false;
    }
    note(expanded.size() == 1 ? ("Deleted element " + expanded.first())
                              : QString("Deleted selection (%1 elements)").arg(expanded.size()));
    edited(QString(), anyTracked);
    return true;
}

bool Controller::spliceElement(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad) {
    return spliceElements({element}, linkSrcElement, linkSrcPad);
}

bool Controller::spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad) {
    if (busy() || elements.isEmpty()) return false;
    QStringList expanded = expandSelectionWithGroups(elements);
    if (expanded.isEmpty()) return false;

    pushUndo();
    // If multiple unconnected elements are dragged together onto a link, connect them in left-to-right order first.
    if (expanded.size() > 1) {
        linkOrderedInternal(expanded);
    }

    QList<PadView> extSinks, extSrcs;
    subgraphExternalPads(expanded, &extSinks, &extSrcs);

    std::vector<std::pair<std::string, std::string>> freeSinks, freeSrcs;
    for (const PadView& p : extSinks) {
        if (p.state != PadView::Linked) freeSinks.emplace_back(toStd(p.element), toStd(p.templateName));
    }
    if (expanded.size() == 1) {
        const QString& only = expanded.first();
        if (const ElementInfo* inf = info(view(only).factory)) {
            for (const PadTemplateInfo& t : inf->sinkTemplates()) {
                if (t.isRequest()) {
                    std::pair<std::string, std::string> cand{toStd(only), toStd(t.name)};
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

    if (!plan_.spliceChain(toStd(linkSrcElement), toStd(linkSrcPad), freeSinks, freeSrcs)) {
        undoStack_.pop_back();
        return false;
    }
    note("Spliced " + expanded.join(", ") + " into link from " + linkSrcElement + "." + linkSrcPad);
    edited(expanded.first(), plan_.isElementTracked(toStd(linkSrcElement), traits()));
    return true;
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (busy() || !info(fromStd(gps::kQueueFactory))) return false;
    pushUndo();
    std::string name = plan_.insertQueue(toStd(element), toStd(pad));
    if (name.empty()) {
        undoStack_.pop_back();
        return false;
    }
    makeRoomForQueue(element.toStdString(), name);
    note("Inserted a queue after " + element + "." + pad, padText(element, pad));
    edited(fromStd(name), plan_.isElementTracked(name, traits()));
    return true;
}

// Plan::insertQueue puts the queue half way between its neighbours, on top of both when they are one column apart.
// Here it takes the column after its source, on the row of the box it feeds, and that box and everything behind it
// move one column right, so every link keeps running left to right.
void Controller::makeRoomForQueue(const std::string& source, const std::string& queue) {
    int out = plan_.linkFrom(queue, gps::kQueueSrcPad);
    gps::PlanElement* from = plan_.find(source);
    gps::PlanElement* q = plan_.find(queue);
    if (out < 0 || !from || !q) return;
    gps::PlanElement* sink = plan_.find(plan_.links[out].sinkElement);
    if (!sink) return;
    q->x = from->x + metrics::kColumn;
    q->y = sink->y;
    double need = q->x + metrics::kColumn - sink->x;
    if (need <= 0) return;

    std::set<std::string> behind = {sink->name};
    for (bool grew = true; grew;) {
        grew = false;
        for (const gps::PlanLink& l : plan_.links) {
            if (behind.count(l.srcElement) && behind.insert(l.sinkElement).second) grew = true;
        }
    }
    for (gps::PlanElement& e : plan_.elements) {
        if (behind.count(e.name)) e.x += need;
    }
}

bool Controller::removeQueue(const QString& queue) {
    if (busy()) return false;
    bool wasTracked = plan_.isElementTracked(toStd(queue), traits());
    pushUndo();
    if (!plan_.removeQueue(toStd(queue))) {
        undoStack_.pop_back();
        return false;
    }
    note("Removed queue " + queue);
    edited(QString(), wasTracked);
    return true;
}

void Controller::setPosition(const QString& element, double x, double y) {
    setPositions({{element, QPointF(x, y)}});
}

void Controller::setPositions(const QList<QPair<QString, QPointF>>& positions) {
    if (positions.isEmpty()) return;
    bool moved = false;
    QSet<QString> handledGroups;
    for (const auto& [element, pt] : positions) {
        gps::PlanElement* e = plan_.find(toStd(element));
        if (!e) continue;
        double dx = pt.x() - e->x;
        double dy = pt.y() - e->y;
        if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) continue;
        QString grp = fromStd(e->group);
        if (!grp.isEmpty() && !isGroupExpanded(grp)) {
            if (handledGroups.contains(grp)) continue;
            handledGroups.insert(grp);
            for (const QString& m : groupMembers(grp)) {
                if (gps::PlanElement* me = plan_.find(toStd(m))) {
                    me->x += dx;
                    me->y += dy;
                    moved = true;
                }
            }
        } else {
            e->x = pt.x();
            e->y = pt.y();
            moved = true;
        }
    }
    if (!moved) return;
    dirty_ = true;
    emit changed();
}

bool Controller::linkOrderedInternal(const QStringList& expanded, QString* lastTouched) {
    if (expanded.size() < 2) return false;
    std::vector<QString> sorted(expanded.begin(), expanded.end());
    std::stable_sort(sorted.begin(), sorted.end(), [this](const QString& a, const QString& b) {
        const gps::PlanElement* ea = plan_.find(toStd(a));
        const gps::PlanElement* eb = plan_.find(toStd(b));
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
        if (plan_.wouldCycle(toStd(srcEl), toStd(snkEl))) continue;
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
            if (const ElementInfo* inf = info(dv.factory)) {
                for (const PadTemplateInfo& t : inf->sinkTemplates()) {
                    if (t.isRequest()) {
                        sinkPad = t.name;
                        break;
                    }
                }
            }
        }
        if (!srcPad.isEmpty() && !sinkPad.isEmpty()) {
            if (plan_.linkExisting(toStd(srcEl), toStd(srcPad), toStd(snkEl), toStd(sinkPad))) {
                linkedAny = true;
                if (lastTouched) *lastTouched = snkEl;
            }
        }
    }
    return linkedAny;
}

bool Controller::connectSelectedElements(const QStringList& names) {
    if (busy() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;

    pushUndo();
    QString lastTouched;
    if (!linkOrderedInternal(expanded, &lastTouched)) {
        undoStack_.pop_back();
        return false;
    }
    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan_.isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    note("Connected selected elements");
    edited(lastTouched, anyTracked);
    return true;
}

bool Controller::disconnectBetweenElements(const QStringList& names) {
    if (busy() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    std::set<std::string> chosen;
    for (const QString& n : expanded) chosen.insert(toStd(n));

    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan_.isElementTracked(toStd(n), traits())) anyTracked = true;
    }

    pushUndo();
    size_t before = plan_.links.size();
    plan_.links.erase(std::remove_if(plan_.links.begin(), plan_.links.end(),
                                     [&](const gps::PlanLink& l) { return chosen.count(l.srcElement) && chosen.count(l.sinkElement); }),
                      plan_.links.end());
    if (plan_.links.size() == before) {
        undoStack_.pop_back();
        return false;
    }
    note("Disconnected links between selected elements");
    edited(QString(), anyTracked);
    return true;
}

bool Controller::alignElementsHorizontally(const QStringList& names) {
    if (busy() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;
    pushUndo();
    double sumY = 0;
    std::vector<gps::PlanElement*> elems;
    for (const QString& n : expanded) {
        if (gps::PlanElement* e = plan_.find(toStd(n))) {
            elems.push_back(e);
            sumY += e->y;
        }
    }
    if (elems.size() < 2) {
        undoStack_.pop_back();
        return false;
    }
    double avgY = std::round(sumY / elems.size());
    std::sort(elems.begin(), elems.end(), [](const gps::PlanElement* a, const gps::PlanElement* b) { return a->x < b->x; });
    double curX = elems.front()->x;
    for (size_t i = 0; i < elems.size(); ++i) {
        elems[i]->y = avgY;
        if (i > 0 && elems[i]->x < curX + metrics::kColumn) {
            elems[i]->x = curX + metrics::kColumn;
        }
        curX = elems[i]->x;
    }
    dirty_ = true;
    note("Aligned selected elements horizontally");
    emit changed();
    return true;
}

bool Controller::alignElementsVertically(const QStringList& names) {
    if (busy() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;
    pushUndo();
    double sumX = 0;
    std::vector<gps::PlanElement*> elems;
    for (const QString& n : expanded) {
        if (gps::PlanElement* e = plan_.find(toStd(n))) {
            elems.push_back(e);
            sumX += e->x;
        }
    }
    if (elems.size() < 2) {
        undoStack_.pop_back();
        return false;
    }
    double avgX = std::round(sumX / elems.size());
    std::sort(elems.begin(), elems.end(), [](const gps::PlanElement* a, const gps::PlanElement* b) { return a->y < b->y; });
    double curY = elems.front()->y;
    for (size_t i = 0; i < elems.size(); ++i) {
        elems[i]->x = avgX;
        if (i > 0) {
            double prevH = view(fromStd(elems[i - 1]->name)).height();
            if (elems[i]->y < curY + prevH + metrics::kGap) {
                elems[i]->y = curY + prevH + metrics::kGap;
            }
        }
        curY = elems[i]->y;
    }
    dirty_ = true;
    note("Aligned selected elements vertically");
    emit changed();
    return true;
}

gps::Removal Controller::previewCut(const QString& element, const QString& pad) const {
    return plan_.previewCut(toStd(element), toStd(pad), traits());
}

gps::Removal Controller::previewReplace(const QString& element, const QString& pad) const {
    return plan_.previewReplace(toStd(element), toStd(pad), traits());
}

gps::Removal Controller::previewDeleteSource(const QString& element) const {
    return plan_.previewDeleteSource(toStd(element), traits());
}

// ---- update

void Controller::startUpdate() {
    cancelled_ = false;
    if (plan_.empty()) {
        result_ = UpdateResult();
        emit changed();
        emit updateFinished(true);
        return;
    }
    gps::Plan tracked = plan_.trackedSubplan(traits());
    if (tracked.empty()) {
        result_ = UpdateResult();
        result_.status = "ok";
        result_.complete = false;
        emit changed();
        emit updateFinished(true);
        return;
    }
    QString file = tmp_.filePath("plan.conf");
    std::string error;
    if (!tracked.save(toStd(file), &error)) {
        result_ = UpdateResult::failure("error", "Unable to write the temporary plan file: " + fromStd(error), QString());
        emit changed();
        emit updateFinished(false);
        return;
    }
    updating_ = true;
    watch_->start();
    emit changed();
    updateClient_ = runHelper("gps-update", {file, "--timeout-ms", QString::number(updateTimeoutMs_)}, updateTimeoutMs_ + 2000,
                              [this](const HelperResult& r) { finishUpdate(r); });
}

void Controller::cancelUpdate() {
    if (!updating_ || !updateClient_ || cancelling_) return;
    cancelling_ = true;
    log("Validation cancelled: stopping the validator");
    updateClient_->cancel();
    emit changed();
}

void Controller::waitMore() {
    watch_->extend();
    emit changed();
}

void Controller::finishUpdate(const HelperResult& r) {
    updating_ = false;
    cancelling_ = false;
    updateClient_ = nullptr;
    watch_->stop();
    QString suspect = touched_;
    QString who = suspect.isEmpty() ? QString("The validator") : QString("Element %1").arg(suspect);
    if (r.outcome == HelperResult::Cancelled) {
        cancelled_ = true;  // the old result stays on the canvas, dimmed; Save and Run wait for a new validation
        log("Validation cancelled — press Validate to run it again");
        emit changed();
        emit updateFinished(false);
        return;
    }
    switch (r.outcome) {
    case HelperResult::Crashed:
        result_ = UpdateResult::failure("crashed", QString("%1 made the validator stop unexpectedly (signal %2).").arg(who).arg(r.exitCode), suspect);
        break;
    case HelperResult::TimedOut:
        result_ = UpdateResult::failure("hung", QString("%1 did not answer in time; the validator was stopped.").arg(who), suspect);
        break;
    case HelperResult::NotFound:
        result_ = UpdateResult::failure("error", "gps-update was not found (set GPS_BIN_DIR or put it next to gstpipestudio).", QString());
        break;
    default:
        if (r.hasJson && !r.json["status"].toString().isEmpty()) {
            result_ = UpdateResult::fromJson(r.json);
        } else {
            result_ = UpdateResult::failure("stopped", QString("The validator stopped unexpectedly (exit code %1).").arg(r.exitCode), QString());
        }
        break;
    }
    log("Validation: " + result_.statusText() + (result_.message.isEmpty() ? "" : " – " + result_.message));
    arrange();
    emit changed();
    emit updateFinished(result_.ok());
}

bool Controller::refresh() {
    if (busy() || plan_.empty()) return false;
    note("Validated the pipeline again");
    startUpdate();
    return true;
}

// ---- files and runs

void Controller::newPlan() {
    if (busy()) return;
    undoStack_.clear();
    redoStack_.clear();
    plan_ = gps::Plan();
    result_ = UpdateResult();
    cancelled_ = false;
    path_.clear();
    dirty_ = false;
    emit changed();
}

void Controller::open(const QString& path) {
    if (busy()) return;
    auto loaded = std::make_shared<gps::Plan>();
    std::string error;
    if (!gps::Plan::load(toStd(path), loaded.get(), &error)) {
        emit notice("Unable to open the file: " + fromStd(error));
        return;
    }
    QStringList factories;
    for (const gps::PlanElement& e : loaded->elements) {
        if (!factories.contains(fromStd(e.factory))) factories << fromStd(e.factory);
    }
    opening_ = true;
    fetchInfos(factories, [this, loaded, path](bool ok) {
        opening_ = false;
        if (!ok) {
            emit notice("Unable to open the file: the plan uses an element this installation does not have.");
            emit changed();
            return;
        }
        undoStack_.clear();
        redoStack_.clear();
        plan_ = *loaded;
        path_ = path;
        dirty_ = false;
        result_ = UpdateResult();
        note("Opened a plan file");
        emit changed();
        startUpdate();
    });
}

bool Controller::save(const QString& path, QString* error) {
    QString target = path.isEmpty() ? path_ : path;
    if (!canSave() || target.isEmpty()) {
        if (error) *error = "Unable to save: " + (saveBlockedReason().isEmpty() ? QString("no file name") : saveBlockedReason());
        return false;
    }
    plan_.complete = result_.isComplete();
    plan_.name = toStd(QFileInfo(target).completeBaseName());
    std::string failure;
    if (!plan_.save(toStd(target), &failure)) {
        if (error) *error = fromStd(failure);
        return false;
    }
    path_ = target;
    dirty_ = false;
    emit changed();
    return true;
}

bool Controller::run() {
    if (!canRun()) return false;
    gps::Plan copy = plan_;
    copy.complete = true;
    QString file = tmp_.filePath("run.conf");
    std::string error;
    if (!copy.save(toStd(file), &error)) {
        emit notice("Unable to write the temporary plan file: " + fromStd(error));
        return false;
    }
    running_ = true;
    log("Running…");
    runClient_ = runHelper("gps-run", {file}, 0, [this](const HelperResult& r) {
        running_ = false;
        runClient_ = nullptr;
        QString status;
        if (r.outcome == HelperResult::Finished && r.hasJson) status = r.json["status"].toString();
        else if (r.outcome == HelperResult::Crashed) status = "crashed";
        else if (r.outcome == HelperResult::NotFound) status = "not-found";
        else status = "error";
        QString message = r.hasJson ? r.json["message"].toString() : QString();
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
