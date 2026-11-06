// controller.cpp – see controller.h. Rules come from gps_pipeline; this file only sequences them with the helpers.
#include "controller.h"

#include "capsparse.h"
#include "actions.h"
#include "linkitem.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
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
    size_t pinned = parseCapsChoice(caps).fields.size();
    return pinned == 0 ? QString("caps") : QString("caps · %1").arg(pinned);
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
    if (plan().empty()) return "The canvas is empty";
    if (updating_) return "Validation is running";
    if (running_) return "The pipeline is running";
    if (monitoring_) return "Monitoring is running";
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
    const gps::Element* e = plan().find(toStd(element));
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
    for (const gps::ExposedParam& p : plan().exposed) text += " " + fromStd(p.name) + "=VALUE";
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
            gps::FactoryDefinition definition;
            definition.isSource = infos_[factory].isSource;
            for (const auto& t : infos_[factory].padTemplates) {
                gps::Pad pad;
                pad.identity = pad.templateName = toStd(t.name);
                pad.direction = t.isSrc() ? gps::Pad::Direction::Source : gps::Pad::Direction::Sink;
                pad.presence = t.presence == "always" ? gps::Pad::Presence::Always : t.presence == "request" ? gps::Pad::Presence::Request : gps::Pad::Presence::Sometimes;
                pad.templateCaps = toStd(t.caps);
                definition.templates.push_back(std::move(pad));
            }
            pipeline_.registerFactory(toStd(factory), std::move(definition));
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
    if (editingBlocked()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty()) return false;
    QString cleanTitle = groupTitle.trimmed();
    if (cleanTitle.isEmpty()) cleanTitle = "CustomGroup";
    cleanTitle.replace('#', '_');
    QString groupId = cleanTitle + "#" + QString::number(nextGroupId_++);
    beginTransaction();
    for (const QString& n : expanded) {
        if (gps::Element* e = editGraph().find(toStd(n))) e->group = toStd(groupId);
    }
    expandedGroups_.remove(groupId);
    dirty_ = true;
    if (!commitTransaction()) return false;
    note("Grouped elements as " + cleanTitle);
    emit changed();
    return true;
}

bool Controller::ungroupElements(const QString& groupId) {
    if (editingBlocked() || groupId.isEmpty()) return false;
    QStringList members = groupMembers(groupId);
    if (members.isEmpty()) return false;
    beginTransaction();
    for (const QString& m : members) {
        if (gps::Element* e = editGraph().find(toStd(m))) e->group.clear();
    }
    expandedGroups_.remove(groupId);
    dirty_ = true;
    if (!commitTransaction()) return false;
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
    QString groupId = customName + "#" + QString::number(nextGroupId_++);
    for (gps::Element& e : copy.elements) e.group = toStd(groupId);
    expandedGroups_.remove(groupId);
    QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : freeSpot();
    return pasteSubplan(copy, at.x(), at.y());
}

QStringList Controller::addCustomElementAfter(const QString& srcElement, const QString& srcPad, const QString& customName) {
    const gps::Graph* tmpl = customElementPlan(customName);
    const gps::Element* from = plan().find(toStd(srcElement));
    if (!tmpl || !from || editingBlocked()) return {};
    gps::Graph copy = *tmpl;
    QString groupId = customName + "#" + QString::number(nextGroupId_++);
    for (gps::Element& e : copy.elements) e.group = toStd(groupId);
    expandedGroups_.remove(groupId);

    // Ensure all factories are cached, then insert and link the entry element.
    QStringList missing;
    for (const gps::Element& e : copy.elements) {
        QString fac = fromStd(e.factory);
        if (!info(fac) && !missing.contains(fac)) missing << fac;
    }
    if (!missing.isEmpty()) {
        fetchInfos(missing, [this, srcElement, srcPad, customName](bool ok) {
            if (ok) addCustomElementAfter(srcElement, srcPad, customName);
        });
        return {};
    }
    beginTransaction();
    double baseX = from->x + metrics::kColumn;
    double baseY = from->y;
    double minX = copy.elements.front().x;
    double minY = copy.elements.front().y;
    for (const gps::Element& e : copy.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }

    std::map<std::string, std::string> nameMap;
    QStringList createdNames;
    for (const gps::Element& e : copy.elements) {
        std::string created = editGraph().addElement(e.factory, e.props, baseX + (e.x - minX), baseY + (e.y - minY));
        if (gps::Element* pe = editGraph().find(created)) pe->group = toStd(groupId);
        ElementEdit edit;
        edit.props = e.props;
        edit.requestSrc = e.requestSrc;
        edit.requestSink = e.requestSink;
        applyEdit(created, edit);
        nameMap[e.name] = created;
        createdNames << fromStd(created);
    }
    for (const gps::Connection& l : copy.links) {
        auto itSrc = nameMap.find(l.srcElement);
        auto itSnk = nameMap.find(l.sinkElement);
        if (itSrc != nameMap.end() && itSnk != nameMap.end()) {
            editGraph().linkExisting(itSrc->second, l.srcPad, itSnk->second, l.sinkPad, l.caps, l.sinkPadProps);
        }
    }

    // Link srcElement.srcPad to the first free sink pad of the custom element group.
    ElementView gv = groupView(groupId);
    for (const PadView& p : gv.sinks) {
        if (p.state != PadView::Linked) {
            editGraph().linkExisting(toStd(srcElement), toStd(srcPad), toStd(p.element), toStd(p.templateName));
            break;
        }
    }
    note("Added custom element " + customName + " after " + srcElement + "." + srcPad);
    bool anyTracked = false;
    for (const QString& n : createdNames) {
        if (plan().isElementTracked(toStd(n), traits())) anyTracked = true;
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
    const gps::Element* element = plan().find(toStd(name));
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
        bool exposed = std::any_of(plan().exposed.begin(), plan().exposed.end(),
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
    v.isTracked = plan().isElementTracked(element->name, traits());
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
            for (size_t i = 0; i < plan().links.size(); ++i) {
                if (plan().links[i].sinkElement == element->name && plan().links[i].sinkPad == toStd(t.name)) link = static_cast<int>(i);
            }
            v.sinks << make(t.name, link, 0);
        } else {
            int k = 0;
            for (size_t i = 0; i < plan().links.size(); ++i) {
                if (plan().links[i].sinkElement == element->name && plan().links[i].sinkPad == toStd(t.name)) {
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
        for (const gps::Connection& l : plan().links) {
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
            p.link = plan().linkFrom(toStd(name), toStd(identity));
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
    if (link < 0 || link >= static_cast<int>(plan().links.size())) return nullptr;
    const gps::Connection& l = plan().links[link];
    const gps::Element* sink = plan().find(l.sinkElement);
    const ElementInfo* inf = sink ? info(fromStd(sink->factory)) : nullptr;
    return inf ? inf->padTemplate(fromStd(l.sinkPad), "sink") : nullptr;
}

QList<ExistingTarget> Controller::existingTargets(const QString& element, const QString& identity) const {
    QList<ExistingTarget> out;
    QList<StructureInfo> allowed = padStructures(element, identity);
    for (const gps::Element& e : plan().elements) {
        const ElementInfo* inf = info(fromStd(e.factory));
        if (!inf || plan().wouldCycle(toStd(element), e.name)) continue;
        for (const PadTemplateInfo& t : inf->sinkTemplates()) {
            if (!capsCompatible(allowed, t.structures)) continue;
            if (!t.isRequest() && plan().sinkPadTaken(e.name, toStd(t.name))) continue;
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

    std::set<std::string> templates;
    for (const auto& entry : e->requestSrc) templates.insert(entry.first);
    for (const auto& entry : edit.requestSrc) templates.insert(entry.first);
    for (const std::string& t : templates) {
        auto wanted = edit.requestSrc.find(t);
        int count = wanted == edit.requestSrc.end() ? 0 : wanted->second;
        if (!editGraph().setRequestSrc(element, t, count)) emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
    }

    std::set<std::string> sinkTemplates;
    for (const auto& entry : e->requestSink) sinkTemplates.insert(entry.first);
    for (const auto& entry : edit.requestSink) sinkTemplates.insert(entry.first);
    for (const std::string& t : sinkTemplates) {
        auto wanted = edit.requestSink.find(t);
        int count = wanted == edit.requestSink.end() ? 0 : wanted->second;
        if (!editGraph().setRequestSink(element, t, count)) emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
    }

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

void Controller::beginTransaction() { pipeline_.editor().begin(); }

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
    QStringList missing;
    for (const gps::Element& e : clip.elements) {
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
    for (const gps::Element& e : clip.elements) {
        minX = std::min(minX, e.x);
        minY = std::min(minY, e.y);
    }
    double offsetX = (std::isfinite(targetX)) ? (targetX - minX) : 40.0;
    double offsetY = (std::isfinite(targetY)) ? (targetY - minY) : 40.0;

    beginTransaction();
    std::map<std::string, std::string> nameMap;
    std::map<std::string, std::string> groupMap;
    QStringList createdNames;
    bool anyTracked = false;

    for (const gps::Element& e : clip.elements) {
        std::string created = editGraph().addElement(e.factory, e.props, e.x + offsetX, e.y + offsetY);
        if (!e.group.empty()) {
            auto git = groupMap.find(e.group);
            if (git == groupMap.end()) {
                QString baseTitle = fromStd(e.group).section('#', 0, 0);
                std::string newGrp = toStd(baseTitle + "#" + QString::number(nextGroupId_++));
                git = groupMap.emplace(e.group, newGrp).first;
            }
            if (gps::Element* pe = editGraph().find(created)) pe->group = git->second;
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

    for (const gps::Connection& l : clip.links) {
        auto itSrc = nameMap.find(l.srcElement);
        auto itSnk = nameMap.find(l.sinkElement);
        if (itSrc != nameMap.end() && itSnk != nameMap.end()) {
            editGraph().linkExisting(itSrc->second, l.srcPad, itSnk->second, l.sinkPad, l.caps, l.sinkPadProps);
        }
    }

    for (const QString& n : createdNames) {
        if (plan().isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    if (!createdNames.isEmpty()) {
        note(QString("Pasted %1 element(s)").arg(createdNames.size()));
        edited(createdNames.last(), anyTracked);
    }
    return createdNames;
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
    if (editingBlocked() || !info(factory)) return false;
    beginTransaction();
    QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : freeSpot();
    std::string name = editGraph().addElement(toStd(factory), edit.props, at.x(), at.y());
    note("Added " + factory);
    applyEdit(name, edit);
    bool isTracked = plan().isElementTracked(name, traits());
    edited(fromStd(name), isTracked);
    return true;
}

bool Controller::addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                          const ElementEdit& edit) {
    if (editingBlocked() || !info(factory)) return false;
    const gps::Element* from = plan().find(toStd(element));
    if (!from) return false;
    beginTransaction();
    std::string name = editGraph().addAfter(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props,
                                      from->x + metrics::kColumn, from->y);
    if (name.empty()) {
        pipeline_.editor().rollback();
        return false;
    }
    applyEdit(name, edit);
    placeNewElement(name);
    note("Added " + factory + " after " + element + "." + pad, padText(element, pad) + " -> " + padText(fromStd(name), sinkPad));
    bool isTracked = plan().isElementTracked(name, traits());
    edited(fromStd(name), isTracked);
    return true;
}

bool Controller::linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad) {
    if (editingBlocked()) return false;
    if (element == sinkElement || plan().wouldCycle(toStd(element), toStd(sinkElement))) {
        emit notice(fromStd(gps::Graph::ouroborosMessage(toStd(element), toStd(sinkElement))));
        return false;
    }
    beginTransaction();
    if (!editGraph().linkExisting(toStd(element), toStd(pad), toStd(sinkElement), toStd(sinkPad))) {
        pipeline_.editor().rollback();
        return false;
    }
    note("Linked " + element + "." + pad + " to an existing element", padText(element, pad) + " -> " + padText(sinkElement, sinkPad));
    bool isTracked = plan().isElementTracked(toStd(element), traits()) || plan().isElementTracked(toStd(sinkElement), traits());
    edited(sinkElement, isTracked);
    return true;
}

bool Controller::editElement(const QString& element, const ElementEdit& edit) {
    if (editingBlocked() || !plan().find(toStd(element))) return false;
    beginTransaction();
    editGraph().setProps(toStd(element), edit.props);
    applyEdit(toStd(element), edit);
    const gps::Element* e = plan().find(toStd(element));
    note("Edited the properties of " + element + (e ? " (" + fromStd(e->factory) + ")" : QString()));
    bool isTracked = plan().isElementTracked(toStd(element), traits());
    edited(element, isTracked);
    return true;
}

bool Controller::setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps) {
    if (editingBlocked() || plan().linkFrom(toStd(element), toStd(pad)) < 0) return false;
    beginTransaction();
    editGraph().setCaps(toStd(element), toStd(pad), toStd(caps));
    editGraph().setSinkPadProps(toStd(element), toStd(pad), sinkPadProps);
    note("Changed the caps of " + element + "." + pad, padText(element, pad));
    edited(element, true);
    return true;
}

bool Controller::cut(const QString& element, const QString& pad) {
    QString linking = padText(element, pad);
    if (editingBlocked()) return false;
    beginTransaction();
    if (editGraph().cut(toStd(element), toStd(pad), traits()).links == 0) {
        pipeline_.editor().rollback();
        return false;
    }
    note("Cut the link at " + element + "." + pad, linking);
    edited(element, true);
    return true;
}

bool Controller::disconnectLink(const QString& element, const QString& pad) {
    if (editingBlocked()) return false;
    const bool wasTracked = plan().isElementTracked(toStd(element), traits());
    const QString linking = padText(element, pad);
    gps::DisconnectAction action(toStd(element), toStd(pad));
    if (!pipeline_.editor().execute(action)) return false;
    note("Disconnected the link at " + element + "." + pad, linking);
    edited(element, wasTracked);
    return true;
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (editingBlocked() || !info(factory)) return false;
    QString linking = padText(element, pad);
    beginTransaction();
    gps::Addition added = editGraph().replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, traits());
    if (added.element.empty()) {
        pipeline_.editor().rollback();
        return false;
    }
    note("Replaced the element behind " + element + "." + pad + " with " + factory, linking + " -> " + factory);
    applyEdit(added.element, edit);
    edited(fromStd(added.element), plan().isElementTracked(added.element, traits()));
    return true;
}

bool Controller::deleteSource(const QString& element) {
    return deleteSelection({element}, {}, false);
}

bool Controller::deleteElement(const QString& element, bool autoHeal) {
    return deleteSelection({element}, {}, autoHeal);
}

bool Controller::deleteSelection(const QStringList& elementNames, const QList<QPair<QString, QString>>& links, bool autoHeal) {
    if (editingBlocked()) return false;
    QStringList expanded = expandSelectionWithGroups(elementNames);
    if (expanded.isEmpty() && links.isEmpty()) return false;

    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan().isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    for (const auto& [srcEl, srcPad] : links) {
        Q_UNUSED(srcPad);
        if (plan().isElementTracked(toStd(srcEl), traits())) anyTracked = true;
    }

    int exitTotalSrcPads = 1;
    QList<PadView> extSrcs;
    subgraphExternalPads(expanded, nullptr, &extSrcs);
    if (!extSrcs.isEmpty()) exitTotalSrcPads = extSrcs.size();

    std::vector<std::string> stdNames;
    for (const QString& n : expanded) stdNames.push_back(toStd(n));

    beginTransaction();
    bool changedAny = false;
    for (const auto& [srcEl, srcPad] : links) {
        if (editGraph().disconnectLink(toStd(srcEl), toStd(srcPad))) changedAny = true;
    }
    if (!stdNames.empty()) {
        if (editGraph().deleteElements(stdNames, autoHeal, exitTotalSrcPads)) changedAny = true;
    }
    if (!changedAny) {
        pipeline_.editor().rollback();
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
    edited(expanded.first(), plan().isElementTracked(toStd(linkSrcElement), traits()));
    return true;
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (editingBlocked() || !info(fromStd(gps::kQueueFactory))) return false;
    beginTransaction();
    std::string name = editGraph().insertQueue(toStd(element), toStd(pad));
    if (name.empty()) {
        pipeline_.editor().rollback();
        return false;
    }
    makeRoomForQueue(element.toStdString(), name);
    note("Inserted a queue after " + element + "." + pad, padText(element, pad));
    edited(fromStd(name), plan().isElementTracked(name, traits()));
    return true;
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
    if (editingBlocked()) return false;
    bool wasTracked = plan().isElementTracked(toStd(queue), traits());
    beginTransaction();
    if (!editGraph().removeQueue(toStd(queue))) {
        pipeline_.editor().rollback();
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
    std::vector<gps::Placement> moves;
    for (const auto& entry : updates) moves.push_back(entry.second);
    if (!pipeline_.editor().execute(gps::MoveElementsAction(std::move(moves)))) return;
    dirty_ = true;
    emit changed();
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

    beginTransaction();
    QString lastTouched;
    if (!linkOrderedInternal(expanded, &lastTouched)) {
        pipeline_.editor().rollback();
        return false;
    }
    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan().isElementTracked(toStd(n), traits())) anyTracked = true;
    }
    note("Connected selected elements");
    edited(lastTouched, anyTracked);
    return true;
}

bool Controller::disconnectBetweenElements(const QStringList& names) {
    if (editingBlocked() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    std::set<std::string> chosen;
    for (const QString& n : expanded) chosen.insert(toStd(n));

    bool anyTracked = false;
    for (const QString& n : expanded) {
        if (plan().isElementTracked(toStd(n), traits())) anyTracked = true;
    }

    beginTransaction();
    size_t before = plan().links.size();
    editGraph().links.erase(std::remove_if(editGraph().links.begin(), editGraph().links.end(),
                                     [&](const gps::Connection& l) { return chosen.count(l.srcElement) && chosen.count(l.sinkElement); }),
                      editGraph().links.end());
    if (plan().links.size() == before) {
        pipeline_.editor().rollback();
        return false;
    }
    note("Disconnected links between selected elements");
    edited(QString(), anyTracked);
    return true;
}

bool Controller::alignElementsHorizontally(const QStringList& names) {
    if (editingBlocked() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;
    beginTransaction();
    double sumY = 0;
    std::vector<gps::Element*> elems;
    for (const QString& n : expanded) {
        if (gps::Element* e = editGraph().find(toStd(n))) {
            elems.push_back(e);
            sumY += e->y;
        }
    }
    if (elems.size() < 2) {
        pipeline_.editor().rollback();
        return false;
    }
    double avgY = std::round(sumY / elems.size());
    std::sort(elems.begin(), elems.end(), [](const gps::Element* a, const gps::Element* b) { return a->x < b->x; });
    double curX = elems.front()->x;
    for (size_t i = 0; i < elems.size(); ++i) {
        elems[i]->y = avgY;
        if (i > 0 && elems[i]->x < curX + metrics::kColumn) {
            elems[i]->x = curX + metrics::kColumn;
        }
        curX = elems[i]->x;
    }
    dirty_ = true;
    if (!commitTransaction()) return false;
    note("Aligned selected elements horizontally");
    emit changed();
    return true;
}

bool Controller::alignElementsVertically(const QStringList& names) {
    if (editingBlocked() || names.size() < 2) return false;
    QStringList expanded = expandSelectionWithGroups(names);
    if (expanded.size() < 2) return false;
    beginTransaction();
    double sumX = 0;
    std::vector<gps::Element*> elems;
    for (const QString& n : expanded) {
        if (gps::Element* e = editGraph().find(toStd(n))) {
            elems.push_back(e);
            sumX += e->x;
        }
    }
    if (elems.size() < 2) {
        pipeline_.editor().rollback();
        return false;
    }
    double avgX = std::round(sumX / elems.size());
    std::sort(elems.begin(), elems.end(), [](const gps::Element* a, const gps::Element* b) { return a->y < b->y; });
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
    if (!commitTransaction()) return false;
    note("Aligned selected elements vertically");
    emit changed();
    return true;
}

bool Controller::arrangePipeline() {
    if (editingBlocked() || plan().elements.empty()) return false;
    std::map<std::string, int> indegree, level;
    std::map<std::string, std::vector<std::string>> next;
    for (const auto& e : plan().elements) { indegree[e.name] = 0; level[e.name] = 0; }
    for (const auto& link : plan().links) { ++indegree[link.sinkElement]; next[link.srcElement].push_back(link.sinkElement); }
    std::vector<std::string> ready;
    for (const auto& e : plan().elements) if (!indegree[e.name]) ready.push_back(e.name);
    auto byOldY = [this](const std::string& a, const std::string& b) {
        const auto* x = plan().find(a), *y = plan().find(b);
        return x->y == y->y ? a < b : x->y < y->y;
    };
    std::sort(ready.begin(), ready.end(), byOldY);
    size_t visited = 0;
    while (!ready.empty()) {
        const auto name = ready.front(); ready.erase(ready.begin()); ++visited;
        for (const auto& target : next[name]) {
            level[target] = std::max(level[target], level[name] + 1);
            if (--indegree[target] == 0) { ready.push_back(target); std::sort(ready.begin(), ready.end(), byOldY); }
        }
    }
    if (visited != plan().elements.size()) return false;
    std::map<int, std::vector<std::string>> columns;
    for (const auto& e : plan().elements) columns[level[e.name]].push_back(e.name);
    // Use the caps text actually rendered by LinkItem when reserving the gap between columns.
    QFont font; font.setPointSizeF(7.5);
    QFontMetricsF text(font);
    std::map<int, double> gap;
    for (const auto& link : linkViews()) {
        const int at = level[toStd(link.srcElement)];
        const double width = text.horizontalAdvance(LinkItem::formatSingleLine(link.negotiatedCaps));
        gap[at] = std::max(gap[at], std::min(480.0, width + 90.0));
    }
    QList<QPair<QString, QPointF>> positions;
    double x = 80;
    for (auto& [column, names] : columns) {
        std::sort(names.begin(), names.end(), byOldY);
        double y = 80;
        for (const auto& name : names) {
            positions.append({fromStd(name), QPointF(x, y)});
            y += view(fromStd(name)).height() + 110;
        }
        x += metrics::kBoxWidth + std::max(260.0, gap[column]);
    }
    setPositions(positions);
    note("Arranged pipeline for readable caps and branches");
    return true;
}

gps::Removal Controller::previewCut(const QString& element, const QString& pad) const {
    return plan().previewCut(toStd(element), toStd(pad), traits());
}

gps::Removal Controller::previewReplace(const QString& element, const QString& pad) const {
    return plan().previewReplace(toStd(element), toStd(pad), traits());
}

gps::Removal Controller::previewDeleteSource(const QString& element) const {
    return plan().previewDeleteSource(toStd(element), traits());
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
    if (plan().empty()) {
        updating_ = false; watch_->stop();
        result_ = UpdateResult();
        emit changed();
        emit updateFinished(true);
        return;
    }
    gps::Graph tracked = plan().trackedSubplan(traits());
    if (tracked.empty()) {
        updating_ = false; watch_->stop();
        result_ = UpdateResult();
        result_.status = "ok";
        result_.complete = false;
        emit changed();
        emit updateFinished(true);
        return;
    }
    updating_ = true;
    watch_->start();
    emit changed();
    backend_.setTimeout(updateTimeoutMs_);
    execution_.start(traits(), [this](const gps::ExecutionResult& result) { finishUpdate(result); });
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
    result_ = UpdateResult::fromCore(pipeline_.observations());
    if ((result_.status == "crashed" || result_.status == "hung") && result_.errorElement.isEmpty())
    {
        result_.errorElement = touched_;
        if (!touched_.isEmpty()) result_.message = "Element " + touched_ + ": " + result_.message;
    }
    log("Validation: " + result_.statusText() + (result_.message.isEmpty() ? "" : " – " + result_.message));
    emit changed();
    emit updateFinished(result_.ok());
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
    result_ = UpdateResult();
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
    fetchInfos(factories, [this, loaded, path, imported](bool ok) {
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
        result_ = UpdateResult();
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
    runHelper("gps-import", {"--output", path, "--probe-ms", "1000", description.trimmed()}, 13000,
              [this, path](const HelperResult& result) {
        opening_ = false;
        if (!result.hasJson || result.json.value("status").toString() != "ok") {
            emit notice("Import failed: " + (result.hasJson ? result.json.value("message").toString() : QString("gps-import did not answer")));
            emit changed();
            return;
        }
        const QString name = result.json.value("nativeName").toString();
        const QJsonArray warnings = result.json.value("warnings").toArray();
        log(QString("Imported native pipeline %1: %2 elements, %3 links")
            .arg(name).arg(result.json.value("elements").toInt()).arg(result.json.value("links").toInt()));
        for (const auto& warning : warnings) log("[gps-import] " + warning.toString());
        if (!warnings.isEmpty()) emit notice("Imported with warnings. Review the Log and validate before running.");
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
    saved.complete = result_.isComplete();
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

bool Controller::run() {
    if (!canRun()) return false;
    gps::Graph copy = plan();
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

bool Controller::startMonitoring() {
    if (!canRun()) return false;
    const QString helper = findHelper("gps-monitor");
    if (helper.isEmpty()) { emit notice("gps-monitor was not found"); return false; }
    gps::Graph copy = plan();
    copy.complete = true;
    const QString file = tmp_.filePath("monitor.conf");
    std::string error;
    if (!copy.save(toStd(file), &error)) { emit notice(fromStd(error)); return false; }
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
            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (!document.isObject()) continue;
            const auto object = document.object();
            if (object.value("type").toString() == "sample") emit telemetryReceived(object);
            else if (object.contains("status")) log("Monitor finished: " + object.value("status").toString());
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
