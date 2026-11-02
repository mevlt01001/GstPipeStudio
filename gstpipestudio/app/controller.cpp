// controller.cpp – see controller.h. Rules come from gps_plan; this file only sequences them with the helpers.
#include "controller.h"

#include "capsparse.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRectF>
#include <QRegularExpression>
#include <algorithm>
#include <memory>
#include <set>

namespace {

std::string toStd(const QString& s) { return s.toStdString(); }
QString fromStd(const std::string& s) { return QString::fromStdString(s); }

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
    for (const ListEntry& e : catalog_) {
        if (e.factory == v.factory) v.nvidia = e.nvidia;
    }

    // Changed properties, the exposed ones marked.
    QStringList lines;
    for (const auto& [key, value] : element->props) {
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

    // Sink pads: Always ones are fixed, a Request one exists once per link (SPEC 8).
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
                if (!r.caps.isEmpty()) p.media = mediaOf(r.caps), p.tip = r.caps;
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
            }
            p.state = p.link >= 0 ? PadView::Linked : (r && r->unknownData ? PadView::Unknown : PadView::Free);
            v.srcs << p;
        }
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

void Controller::edited(const QString& touchedElement) {
    dirty_ = true;
    touched_ = touchedElement;
    emit changed();
    startUpdate();
}

bool Controller::addSource(const QString& factory, const ElementEdit& edit) {
    if (busy() || !info(factory)) return false;
    QPointF at = freeSpot();
    std::string name = plan_.addSource(toStd(factory), edit.props, at.x(), at.y());
    note("Added source " + factory);
    applyEdit(name, edit);
    edited(fromStd(name));
    return true;
}

bool Controller::addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                          const ElementEdit& edit) {
    if (busy() || !info(factory)) return false;
    const gps::PlanElement* from = plan_.find(toStd(element));
    if (!from) return false;
    std::string name = plan_.addAfter(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props,
                                      from->x + metrics::kColumn, from->y);
    if (name.empty()) return false;
    applyEdit(name, edit);
    note("Added " + factory + " after " + element + "." + pad, padText(element, pad) + " -> " + padText(fromStd(name), sinkPad));
    edited(fromStd(name));
    return true;
}

bool Controller::linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad) {
    if (busy() || !plan_.linkExisting(toStd(element), toStd(pad), toStd(sinkElement), toStd(sinkPad))) return false;
    note("Linked " + element + "." + pad + " to an existing element", padText(element, pad) + " -> " + padText(sinkElement, sinkPad));
    edited(sinkElement);
    return true;
}

bool Controller::editElement(const QString& element, const ElementEdit& edit) {
    if (busy() || !plan_.setProps(toStd(element), edit.props)) return false;
    applyEdit(toStd(element), edit);
    const gps::PlanElement* e = plan_.find(toStd(element));
    note("Edited the properties of " + element + (e ? " (" + fromStd(e->factory) + ")" : QString()));
    edited(element);
    return true;
}

bool Controller::setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps) {
    if (busy() || !plan_.setCaps(toStd(element), toStd(pad), toStd(caps))) return false;
    plan_.setSinkPadProps(toStd(element), toStd(pad), sinkPadProps);
    note("Changed the caps of " + element + "." + pad, padText(element, pad));
    edited(element);
    return true;
}

bool Controller::cut(const QString& element, const QString& pad) {
    QString linking = padText(element, pad);
    if (busy() || plan_.cut(toStd(element), toStd(pad), traits()).links == 0) return false;
    note("Cut the link at " + element + "." + pad, linking);
    edited(element);
    return true;
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (busy() || !info(factory)) return false;
    QString linking = padText(element, pad);
    gps::Addition added = plan_.replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, traits());
    if (added.element.empty()) return false;
    note("Replaced the element behind " + element + "." + pad + " with " + factory, linking + " -> " + factory);
    applyEdit(added.element, edit);
    edited(fromStd(added.element));
    return true;
}

bool Controller::deleteSource(const QString& element) {
    if (busy() || plan_.deleteSource(toStd(element), traits()).elements.empty()) return false;
    note("Deleted source " + element);
    edited(QString());
    return true;
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (busy() || !info(fromStd(gps::kQueueFactory))) return false;
    std::string name = plan_.insertQueue(toStd(element), toStd(pad));
    if (name.empty()) return false;
    makeRoomForQueue(element.toStdString(), name);
    note("Inserted a queue after " + element + "." + pad, padText(element, pad));
    edited(fromStd(name));
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
    if (busy() || !plan_.removeQueue(toStd(queue))) return false;
    note("Removed queue " + queue);
    edited(QString());
    return true;
}

void Controller::setPosition(const QString& element, double x, double y) {
    gps::PlanElement* e = plan_.find(toStd(element));
    if (!e || (e->x == x && e->y == y)) return;
    e->x = x;
    e->y = y;
    dirty_ = true;
    emit changed();
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
    QString file = tmp_.filePath("plan.conf");
    std::string error;
    if (!plan_.save(toStd(file), &error)) {
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
