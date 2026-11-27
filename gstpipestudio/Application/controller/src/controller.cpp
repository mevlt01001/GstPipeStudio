// controller.cpp – see controller.h. Rules come from gps_pipeline; this file only sequences the sessions with each
// other and with the helpers.
#include "controller.h"

#include "actions.h"
#include "canvasmetrics.h"
#include "templates.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRectF>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace {

// The pads an edit names: a src pad by its identity, a sink pad by what the link names it.
gps::PadRef srcRef(const QString& element, const QString& pad) { return {toStd(element), toStd(pad), gps::Pad::Direction::Source}; }
gps::PadRef sinkRef(const QString& element, const QString& pad) { return {toStd(element), toStd(pad), gps::Pad::Direction::Sink}; }

QString shellQuote(const QString& s) {
    static const QRegularExpression plain("^[A-Za-z0-9_./=:+-]+$");
    return plain.match(s).hasMatch() ? s : "'" + QString(s).replace("'", "'\\''") + "'";
}

}  // namespace

Controller::Controller(AppServices& services, QObject* parent)
    : QObject(parent), services_(services), layout_(new CanvasLayout(*this)),
      templates_(edits_, pipeline_, services.library(), services.catalog(), views_),
      views_(pipeline_, services.catalog(), templates_, *layout_) {
    edits_.setBlocker([this] { return blockedReason(); });
    connect(&edits_, &EditSession::committed, this, &Controller::committed);
    connect(&edits_, &EditSession::notice, this, &Controller::notice);
    connect(&edits_, &EditSession::synced, this, &Controller::synced);
    connect(&validation_, &ValidationSession::changed, this, &Controller::changed);
    connect(&validation_, &ValidationSession::finished, this, &Controller::validated);
    connect(&validation_, &ValidationSession::logLine, this, logger());
    connect(&runs_, &RunSession::changed, this, &Controller::changed);
    connect(&runs_, &RunSession::runFinished, this, &Controller::runFinished);
    connect(&runs_, &RunSession::telemetryReceived, this, &Controller::telemetryReceived);
    connect(&runs_, &RunSession::logLine, this, logger());
    connect(&runs_, &RunSession::notice, this, &Controller::notice);
    connect(&templates_, &TemplateInstances::templateChanged, this, &Controller::templateChanged);
    connect(&templates_, &TemplateInstances::templatesOutdated, this, &Controller::templatesOutdated);
    connect(&templates_, &TemplateInstances::updated, this, [this] { niceAlign(); });
    connect(&templates_, &TemplateInstances::notice, this, &Controller::notice);
    connect(&templates_, &TemplateInstances::logLine, this, logger());
    // The shared catalog already knows the factories other documents asked for; the Pipeline learns them now.
    catalog().forEachInfo([this](const QString& factory, const ElementInfo&) { registerFactory(factory); });
    connect(&catalog(), &FactoryCatalog::infoArrived, this, &Controller::registerFactory);
    connect(&catalog(), &FactoryCatalog::notice, this, &Controller::notice);
    connect(&catalog(), &FactoryCatalog::logLine, this, logger());
    connect(&services_.environment(), &EnvironmentService::checked, this, [this](const StartupResult& r) {
        log(r.state == StartupResult::Ready ? r.statusText() : "GStreamer check: the environment is not ready");
    });
    connect(&services_.library(), &TemplateLibrary::changed, this, &Controller::changed);  // any tab wrote the library
}

void Controller::registerFactory(const QString& factory) {
    pipeline_.registerFactory(toStd(factory), *catalog().definition(factory));
}

// ---- state

// The one edit-blocked guard; Save is off for the same reasons.
QString Controller::blockedReason() const {
    if (opening_) return "A file is being opened";
    if (running()) return "The pipeline is running";
    if (monitoring()) return "Monitoring is running";
    if (cancelling()) return "Validation is being cancelled";
    return QString();
}

QString Controller::busyReason() const {
    if (const QString blocked = blockedReason(); !blocked.isEmpty()) return blocked;
    return updating() ? "Validation is running" : QString();
}

QString Controller::runBlockedReason() const {
    if (const QString own = target_->runBlockedReason(); !own.isEmpty()) return own;
    if (const QString busy = busyReason(); !busy.isEmpty()) return busy;
    if (plan().empty()) return "The canvas is empty";
    if (notValidated()) return "Validation was cancelled — press Validate";
    if (!result().valid()) return "The pipeline is not validated yet — press Validate";
    if (!result().ok()) return "The last validation reported a problem (see the Validation tab)";
    if (!result().complete()) return "The pipeline is not complete: link every free src pad";
    return QString();
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

SupportInfo Controller::supportInfo(const QString& title) const {
    SupportInfo i = SupportReport::systemInfo();
    i.title = title;
    i.action = edits_.lastAction();
    i.linking = edits_.lastLinking();
    if (result().valid()) i.validation = statusText(result()) + (result().message.empty() ? QString() : " – " + fromStd(result().message));
    else if (notValidated()) i.validation = "cancelled";
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
    if (target_->projectConf().isEmpty()) return QString();
    QFileInfo file(target_->projectConf());
    QString text = "GPS_PIPELINE_PATH=" + shellQuote(file.absolutePath()) + " gst-launch-1.0 " + shellQuote(file.completeBaseName());
    for (const gps::ExposedParam& p : plan().exposed) text += " " + fromStd(p.name) + "=VALUE";
    return text;
}

// ---- what one session's work means for the others

// An edit is shown, followed by automatic alignment (not undo and redo: going back is not an edit to follow), validated
// when it changed what runs, and its template copies are checked.
void Controller::committed(const Edit& edit, bool restoring) {
    emit changed();
    if (!restoring) alignAfterEdit();
    if (!edit.presentation) {
        if (pipeline_.lastChange().kind == gps::ChangeKind::Execution || updating()) validation_.request(edit.touched);
        else emit updateFinished(true);
    }
    templates_.check(!restoring);
}

void Controller::validated(bool ok) {
    emit updateFinished(ok);
    if (!notValidated()) alignOpened();
}

// ---- template copies

QString Controller::beginTemplate(const QStringList& elements) {
    const QString id = templates_.begin(elements);
    if (!id.isEmpty()) setTemplateExpanded(id, true);  // its border is drawn, and the canvas makes room
    return id;
}

// Collapsing ends a copy's editing. The box grows into its frame from its centre, or the frame shrinks into it;
// everything around it makes room.
void Controller::setTemplateExpanded(const QString& id, bool expanded) {
    if (id.isEmpty() || isTemplateExpanded(id) == expanded) return;
    if (!expanded) templates_.commit(id);
    const CanvasLayout::Pin pin = layout_->pinOf(outline().membersOf(id), true);
    templates_.setExpanded(id, expanded);
    layout_->invalidate();
    layout_->align({}, pin);
    emit changed();
}

QStringList Controller::place(const ComponentRef& component, double x, double y) {
    const QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : views_.freeSpot();
    const QStringList created = land(component, at, std::nullopt, [=] { place(component, x, y); });
    if (!created.isEmpty()) niceAlign();  // the new box finds its place among the others
    return created;
}

QStringList Controller::placeAfter(const QString& srcElement, const QString& srcPad, const ComponentRef& component) {
    const gps::Element* from = plan().find(toStd(srcElement));
    if (!from) return {};
    return land(component, spotAfter(*from), srcRef(srcElement, srcPad), [=] { placeAfter(srcElement, srcPad, component); });
}

// Where something added after an element's pad goes: one column to its right, on its row.
QPointF Controller::spotAfter(const gps::Element& from) { return {from.x + metrics::kColumn, from.y}; }

// The one way a component lands on the canvas, in one undo step: its factories known (else asked for, and retry runs
// once they are), its content's top left corner at `at`, and fed by `after` through the node's first free sink pad,
// worked out on the draft it is placed in. After a pad it moves down off any box there, as an added element does.
QStringList Controller::land(const ComponentRef& component, const QPointF& at, const std::optional<gps::PadRef>& after,
                             const std::function<void()>& retry) {
    const ComponentProvider* provider = services_.provider(component.provider);
    const gps::Graph content = provider ? provider->peek(component.id) : gps::Graph();
    if (content.empty() || !infosReady(content, retry)) return {};
    QStringList created;
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        const std::optional<gps::NodeRef> node = provider->place(g, component.id, at.x(), at.y());
        if (!node) return false;
        const gps::NodeShape shape(g, pipeline_);
        created = fromStd(shape.members(*node));
        edit.action = "Added " + component.provider + " " + component.id;
        if (after) {
            if (const auto entry = shape.firstFree(*node, gps::Pad::Direction::Sink))
                g.linkExisting(after->element, after->identity, entry->element, entry->linkName());
            placeNew(g, toStd(created));
            edit.action += " after " + fromStd(after->element) + "." + fromStd(after->identity);
        }
        edit.touched = created.value(0);
        return true;
    });
    return created;
}

// The clip's factories are known (true), or are asked for and retry runs once they are (false).
bool Controller::infosReady(const gps::Graph& clip, std::function<void()> retry) {
    if (catalog().missing(clip).isEmpty()) return true;
    catalog().fetchMissing(clip, this, [retry](bool ok) {
        if (ok) retry();
    });
    return false;
}

// ---- edits

// Request pad counts, src and sink alike: every template the element or the edit names gets the edit's count.
void Controller::applyEdit(gps::Graph& g, const std::string& element, const ElementEdit& edit) {
    gps::Element* e = g.find(element);
    if (!e) return;
    auto counts = [&](const std::map<std::string, int>& now, const std::map<std::string, int>& wanted,
                      bool (gps::Graph::*set)(const std::string&, const std::string&, int)) {
        std::set<std::string> templates;
        for (const auto& entry : now) templates.insert(entry.first);
        for (const auto& entry : wanted) templates.insert(entry.first);
        for (const std::string& t : templates) {
            auto found = wanted.find(t);
            int count = found == wanted.end() ? 0 : found->second;
            if (!(g.*set)(element, t, count))
                emit notice(fromStd("Unable to set the pad count of " + t + " to " + std::to_string(count) + ": linked pads need it."));
        }
    };
    counts(e->requestSrc, edit.requestSrc, &gps::Graph::setRequestSrc);
    counts(e->requestSink, edit.requestSink, &gps::Graph::setRequestSink);
}

// Automatic placement moves only what was just created, as one block, down until it overlaps no other box. Native
// observations never rearrange a user's canvas.
void Controller::placeNew(gps::Graph& g, const std::vector<std::string>& created) {
    const std::set<std::string> mine(created.begin(), created.end());
    auto boxOf = [this](const gps::Element& e) { return QRectF(e.x, e.y, metrics::kBoxWidth, view(fromStd(e.name)).height()); };
    std::vector<gps::Element*> moving;
    QRectF block;
    for (const std::string& name : created) {
        if (gps::Element* e = g.find(name)) {
            moving.push_back(e);
            block |= boxOf(*e);
        }
    }
    if (moving.empty()) return;
    const double top = block.top();
    const double margin = metrics::kGap / 2;
    for (bool moved = true; moved;) {
        moved = false;
        for (const gps::Element& other : g.elements) {
            if (mine.count(other.name)) continue;
            const QRectF occupied = boxOf(other);
            if (block.adjusted(-margin, -margin, margin, margin).intersects(occupied)) {
                block.moveTop(occupied.bottom() + metrics::kGap);
                moved = true;
            }
        }
    }
    for (gps::Element* e : moving) e->y += block.top() - top;
}

gps::Graph Controller::copySubplan(const QStringList& elementNames) const {
    return gps::GraphFragment(toStd(outline().expand(elementNames))).copy(plan());
}

QStringList Controller::pasteSubplan(const gps::Graph& clip, double targetX, double targetY) {
    if (clip.elements.empty()) return {};
    if (!infosReady(clip, [=] { pasteSubplan(clip, targetX, targetY); })) return {};
    const auto [left, top] = clip.topLeft();
    const QPointF corner(left, top);
    const QPointF at = std::isfinite(targetX) ? QPointF(targetX, targetY) : corner + QPointF(40, 40);
    QStringList created;
    edits_.transact([&](gps::Graph& g, Edit& edit) {
        const QPointF offset = at - corner;
        for (const auto& [old, made] : gps::templates::insertCopy(g, clip, offset.x(), offset.y())) {
            if (g.find(made)) created << fromStd(made);
        }
        edit.action = QString("Pasted %1 element(s)").arg(created.size());
        edit.touched = created.last();
        return true;
    });
    return created;
}

QStringList Controller::duplicateElements(const QStringList& names) {
    if (names.isEmpty()) return {};
    return pasteSubplan(copySubplan(names));
}

bool Controller::addElement(const QString& factory, const ElementEdit& edit, double x, double y) {
    if (!catalog().info(factory)) return false;
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        QPointF at = (std::isfinite(x) && std::isfinite(y)) ? QPointF(x, y) : views_.freeSpot();
        std::string name = g.addElement(toStd(factory), edit.props, at.x(), at.y());
        applyEdit(g, name, edit);
        e.action = "Added " + factory;
        e.touched = fromStd(name);
        return true;
    });
}

bool Controller::addAfter(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                          const ElementEdit& edit) {
    const gps::Element* from = plan().find(toStd(element));
    if (!catalog().info(factory) || !from) return false;
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        const QPointF at = spotAfter(*from);
        std::string name = g.addAfter(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, at.x(), at.y());
        if (name.empty()) return false;
        applyEdit(g, name, edit);
        placeNew(g, {name});
        e.action = "Added " + factory + " after " + element + "." + pad;
        e.linking = padName(srcRef(element, pad)) + " -> " + padName(sinkRef(fromStd(name), sinkPad));
        e.touched = fromStd(name);
        return true;
    });
}

bool Controller::linkExisting(const QString& element, const QString& pad, const QString& sinkElement, const QString& sinkPad) {
    if (element == sinkElement || plan().wouldCycle(toStd(element), toStd(sinkElement))) {
        emit notice(fromStd(gps::Graph::ouroborosMessage(toStd(element), toStd(sinkElement))));
        return false;
    }
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        if (!g.linkExisting(toStd(element), toStd(pad), toStd(sinkElement), toStd(sinkPad))) return false;
        e.action = "Linked " + element + "." + pad + " to an existing element";
        e.linking = padName(srcRef(element, pad)) + " -> " + padName(sinkRef(sinkElement, sinkPad));
        e.touched = sinkElement;
        return true;
    });
}

bool Controller::editElement(const QString& element, const ElementEdit& edit) {
    const gps::Element* found = plan().find(toStd(element));
    if (!found) return false;
    const QString factory = fromStd(found->factory);
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        g.setProps(toStd(element), edit.props);
        applyEdit(g, toStd(element), edit);
        e.action = "Edited the properties of " + element + " (" + factory + ")";
        e.touched = element;
        return true;
    });
}

bool Controller::setCaps(const QString& element, const QString& pad, const QString& caps, const gps::Props& sinkPadProps) {
    if (plan().linkFrom(toStd(element), toStd(pad)) < 0) return false;
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        g.setCaps(toStd(element), toStd(pad), toStd(caps));
        g.setSinkPadProps(toStd(element), toStd(pad), sinkPadProps);
        e = {"Changed the caps of " + element + "." + pad, padName(srcRef(element, pad)), element};
        return true;
    });
}

bool Controller::cut(const QString& element, const QString& pad) {
    const QString linking = padName(srcRef(element, pad));
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        e = {"Cut the link at " + element + "." + pad, linking, element};
        return g.cut(toStd(element), toStd(pad), catalog().traits()).links > 0;
    });
}

bool Controller::disconnectLink(const QString& element, const QString& pad) {
    return edits_.perform(gps::DisconnectAction(toStd(element), toStd(pad)),
                          {"Disconnected the link at " + element + "." + pad, padName(srcRef(element, pad)), element});
}

bool Controller::replace(const QString& element, const QString& pad, const QString& factory, const QString& sinkPad,
                         const ElementEdit& edit) {
    if (!catalog().info(factory)) return false;
    const QString linking = padName(srcRef(element, pad));
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        gps::Addition added = g.replace(toStd(element), toStd(pad), toStd(factory), toStd(sinkPad), edit.props, catalog().traits());
        if (added.element.empty()) return false;
        applyEdit(g, added.element, edit);
        e.action = "Replaced the element behind " + element + "." + pad + " with " + factory;
        e.linking = linking + " -> " + factory;
        e.touched = fromStd(added.element);
        return true;
    });
}

bool Controller::deleteSelection(const QStringList& elementNames, const QList<gps::LinkRef>& links, bool autoHeal) {
    const QStringList expanded = outline().expand(elementNames);
    if (expanded.isEmpty() && links.isEmpty()) return false;
    const std::vector<std::string> names = toStd(expanded);
    const size_t exits = shape().ports(names).srcs.size();
    const int exitTotalSrcPads = exits == 0 ? 1 : static_cast<int>(exits);
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        bool changedAny = false;
        for (const gps::LinkRef& link : links) changedAny = g.disconnectLink(link.src.element, link.src.identity) || changedAny;
        if (!names.empty()) changedAny = g.deleteElements(names, autoHeal, exitTotalSrcPads) || changedAny;
        e.action = expanded.size() == 1 ? "Deleted element " + expanded.first() : QString("Deleted selection (%1 elements)").arg(expanded.size());
        return changedAny;
    });
}

// Its free pads are where the link goes in and comes out; a single element may also take a new Request pad.
bool Controller::spliceElements(const QStringList& elements, const QString& linkSrcElement, const QString& linkSrcPad) {
    const QStringList expanded = outline().expand(elements);
    if (expanded.isEmpty()) return false;
    std::vector<std::string> ids = toStd(expanded);
    if (!gps::GraphFragment(ids).connected(plan())) return false;

    const gps::NodePorts ports = shape().ports(ids);
    std::vector<std::pair<std::string, std::string>> freeSinks, freeSrcs;
    std::vector<gps::PadRef> entries = ports.free(gps::Pad::Direction::Sink);
    if (ids.size() == 1) entries.insert(entries.end(), ports.requests.begin(), ports.requests.end());
    for (const gps::PadRef& p : entries) {
        const std::pair<std::string, std::string> entry{p.element, p.linkName()};
        if (std::find(freeSinks.begin(), freeSinks.end(), entry) == freeSinks.end()) freeSinks.push_back(entry);
    }
    for (const gps::PadRef& p : ports.free(gps::Pad::Direction::Source)) freeSrcs.emplace_back(p.element, p.identity);

    const gps::InsertFragmentAction action(gps::GraphFragment(std::move(ids)), {toStd(linkSrcElement), toStd(linkSrcPad)},
                                           std::move(freeSinks), std::move(freeSrcs));
    return edits_.perform(action, {"Spliced " + expanded.join(", ") + " into link from " + linkSrcElement + "." + linkSrcPad, QString(), expanded.first()});
}

bool Controller::insertQueue(const QString& element, const QString& pad) {
    if (!catalog().info(fromStd(gps::kQueueFactory))) return false;
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        std::string name = g.insertQueue(toStd(element), toStd(pad));
        if (name.empty()) return false;
        makeRoomForQueue(g, toStd(element), name);
        e = {"Inserted a queue after " + element + "." + pad, padName(srcRef(element, pad)), fromStd(name)};
        return true;
    });
}

// Graph::insertQueue puts the queue half way between its neighbours, on top of both when they are one column apart.
// Here it takes the column after its source, on the row of the box it feeds, and that box and everything behind it
// move one column right, so every link keeps running left to right.
void Controller::makeRoomForQueue(gps::Graph& g, const std::string& source, const std::string& queue) {
    int out = g.linkFrom(queue, gps::kQueueSrcPad);
    gps::Element* from = g.find(source);
    gps::Element* q = g.find(queue);
    if (out < 0 || !from || !q) return;
    gps::Element* sink = g.find(g.links[out].sinkElement);
    if (!sink) return;
    q->x = from->x + metrics::kColumn;
    q->y = sink->y;
    double need = q->x + metrics::kColumn - sink->x;
    if (need <= 0) return;

    std::set<std::string> behind = {sink->name};
    for (bool grew = true; grew;) {
        grew = false;
        for (const gps::Connection& l : g.links) {
            if (behind.count(l.srcElement) && behind.insert(l.sinkElement).second) grew = true;
        }
    }
    for (gps::Element& e : g.elements) {
        if (behind.count(e.name)) e.x += need;
    }
}

bool Controller::removeQueue(const QString& queue) {
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        e.action = "Removed queue " + queue;
        return g.removeQueue(toStd(queue));
    });
}

// A collapsed template moves whole: its members follow the box that was dragged. One undoable presentation change that
// says nothing of itself: the support report keeps the last real edit.
void Controller::setPositions(const QList<QPair<QString, QPointF>>& positions) {
    if (positions.isEmpty()) return;
    const Outline outline = this->outline();
    QSet<QString> handledGroups;
    std::map<std::string, gps::Placement> updates;
    for (const auto& [element, pt] : positions) {
        const auto* e = plan().find(toStd(element));
        if (!e) continue;
        const double dx = pt.x() - e->x, dy = pt.y() - e->y;
        if (std::abs(dx) < 0.01 && std::abs(dy) < 0.01) continue;
        const QString group = outline.collapsedTemplate(element);
        if (!group.isEmpty()) {
            if (handledGroups.contains(group)) continue;
            handledGroups.insert(group);
            for (const auto& member : outline.membersOf(group)) {
                const auto* other = plan().find(toStd(member));
                updates[other->name] = {other->name, other->x + dx, other->y + dy};
            }
        } else updates[e->name] = {e->name, pt.x(), pt.y()};
    }
    std::vector<gps::Placement> moves;
    for (const auto& entry : updates) moves.push_back(entry.second);
    if (!moves.empty()) {
        if (!layout_->applying()) alignWhenValidated_ = false;
        edits_.perform(gps::MoveElementsAction(std::move(moves)), {QString(), QString(), QString(), true});
    }
}

// Left to right (then sources, filters, sinks, then top to bottom), each element feeds the next one: its first free src
// pad into the next one's first free sink pad, or a new Request pad of it. Worked out on the draft as it grows.
bool Controller::linkInOrder(gps::Graph& g, const QStringList& expanded, QString* lastTouched) {
    const gps::NodeShape shape(g, pipeline_);
    auto rank = [&](const QString& name) {
        const gps::Role role = shape.role(gps::NodeRef::element(toStd(name)));
        return role == gps::Role::Source ? 0 : role == gps::Role::Sink ? 2 : 1;
    };
    std::vector<QString> sorted(expanded.begin(), expanded.end());
    std::stable_sort(sorted.begin(), sorted.end(), [&](const QString& a, const QString& b) {
        const gps::Element* ea = g.find(toStd(a));
        const gps::Element* eb = g.find(toStd(b));
        if (!ea || !eb) return a < b;
        if (std::abs(ea->x - eb->x) > 1.0) return ea->x < eb->x;
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return ea->y < eb->y;
    });

    bool linkedAny = false;
    for (size_t i = 0; i + 1 < sorted.size(); ++i) {
        const std::string from = toStd(sorted[i]), to = toStd(sorted[i + 1]);
        const auto src = shape.firstFree(gps::NodeRef::element(from), gps::Pad::Direction::Source);
        const auto sink = shape.firstFree(gps::NodeRef::element(to), gps::Pad::Direction::Sink);
        if (src && sink && g.linkExisting(src->element, src->identity, sink->element, sink->linkName())) {
            linkedAny = true;
            *lastTouched = sorted[i + 1];
        }
    }
    return linkedAny;
}

bool Controller::connectSelectedElements(const QStringList& names) {
    const QStringList expanded = outline().expand(names);
    if (expanded.size() < 2) return false;
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        e.action = "Connected selected elements";
        return linkInOrder(g, expanded, &e.touched);
    });
}

bool Controller::disconnectBetweenElements(const QStringList& names) {
    if (names.size() < 2) return false;
    std::set<std::string> chosen;
    for (const QString& n : outline().expand(names)) chosen.insert(toStd(n));
    return edits_.transact([&](gps::Graph& g, Edit& e) {
        e.action = "Disconnected links between selected elements";
        return g.disconnectBetween(chosen);
    });
}

// ---- layout

bool Controller::niceAlign(const QStringList& elements) {
    const QStringList chosen = elements.isEmpty() ? QStringList() : outline().expand(elements);
    return layout_->align(chosen, layout_->pinOf(chosen.isEmpty() ? fromStd(plan().elementNames()) : chosen));  // what is on screen stays there
}

// An edit of the user's, with automatic alignment on: the whole canvas follows, once for a burst of edits.
void Controller::alignAfterEdit() {
    if (!services_.view().autoAlign() || layout_->applying() || alignQueued_) return;
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

bool Controller::refresh() {
    if (busy() || plan().empty()) return false;
    edits_.note("Validated the pipeline again");
    validation_.request();
    return true;
}

// ---- files

void Controller::newPlan(std::unique_ptr<SaveTarget> target) {
    if (busy()) return;
    pipeline_.reset();
    templates_.forget();
    validation_.forget();
    target_ = std::move(target);
    edits_.markSaved();
    edits_.note("New " + target_->title());
    emit changed();
}

void Controller::setTarget(std::unique_ptr<SaveTarget> target) {
    target_ = std::move(target);
    emit changed();
}

void Controller::markModified() {
    edits_.markModified(true);
    emit changed();
}

void Controller::restoreSavePoint() {
    edits_.restoreSavePoint();
    emit changed();
}

void Controller::open(std::unique_ptr<SaveTarget> target, const QString& backup) {
    std::shared_ptr<SaveTarget> bound = std::move(target);
    if (backup.isEmpty()) load(bound->path(), bound, Load::Open, "Opened " + bound->title());
    else load(backup, bound, Load::Recover, "Recovered the unsaved changes of " + bound->title());
    bound->opened();
}

void Controller::restoreVersion(const QString& version) { load(version, nullptr, Load::Replace, "Restored an earlier version"); }

void Controller::reload() {
    if (QFileInfo::exists(target_->path())) load(target_->path(), nullptr, Load::Open, "Read " + target_->title() + " from disk");
}

void Controller::revert() {
    if (canRevert()) reload();
}

// A new project has its path before its first save writes the file: there is nothing to go back to yet.
bool Controller::canRevert() const { return !busy() && modified() && QFileInfo::exists(target_->path()); }

void Controller::load(const QString& source, std::shared_ptr<SaveTarget> target, Load how, const QString& action) {
    if (busy()) return;
    auto loaded = std::make_shared<gps::Graph>();
    std::string error;
    std::vector<std::string> libraries;
    for (const QString& folder : (target ? target : target_)->templateFolders()) libraries.push_back(toStd(folder));
    if (!gps::Graph::load(toStd(source), loaded.get(), &error, libraries)) {
        emit notice("Unable to open the file: " + fromStd(error));
        return;
    }
    opening_ = true;
    pending_ = how == Load::Replace ? nullptr : target;
    catalog().fetchMissing(*loaded, this, [this, loaded, target, how, action](bool ok) {
        opening_ = false;
        pending_.reset();
        if (!ok) {
            emit notice("Unable to open the file: the plan uses an element this installation does not have.");
            emit changed();
            return;
        }
        std::string failure;
        if (how != Load::Replace) {
            if (!pipeline_.reset(*loaded, &failure)) {
                emit notice(fromStd(failure));
                emit changed();  // no longer opening: the canvas and the tab title follow
                return;
            }
            if (target) target_ = target;
            if (how == Load::Open) edits_.markSaved();
            else edits_.markModified();
        } else if (!edits_.replace(*loaded)) {
            emit changed();
            return;
        }
        edits_.note(action);
        alignWhenValidated_ = true;
        templates_.forget();
        emit changed();
        layout_->align();
        validation_.request();
        templates_.offer();
    });
}

bool Controller::runImportLaunch(const QString& description, std::function<void(const QString& path)> onReady) {
    if (busy() || description.trimmed().isEmpty()) return false;
    const QString helper = findHelper("gps-import");
    if (helper.isEmpty()) { emit notice("gps-import was not found"); return false; }
    const QString path = tmp_.filePath("import.conf");
    opening_ = true;
    emit changed();
    HelperClient::run(this, "gps-import", {"--output", path, "--probe-ms", "1000", description.trimmed()}, 13000, logger(),
              [this, path, onReady = std::move(onReady)](const HelperResult& result) {
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
        onReady(path);
    });
    return true;
}

bool Controller::importLaunch(const QString& description) {
    return runImportLaunch(description, [this](const QString& path) {
        load(path, nullptr, Load::Replace, "Imported a gst-launch pipeline");
    });
}

bool Controller::addLaunch(const QString& description, double targetX, double targetY,
                           std::function<void(const QStringList&)> onAdded) {
    return runImportLaunch(description, [this, targetX, targetY, onAdded = std::move(onAdded)](const QString& path) {
        auto imported = std::make_shared<gps::Graph>();
        std::string error;
        std::vector<std::string> libraries;
        for (const QString& folder : target_->templateFolders()) libraries.push_back(toStd(folder));
        if (!gps::Graph::load(toStd(path), imported.get(), &error, libraries)) {
            emit notice("Unable to import pipeline: " + fromStd(error));
            emit changed();
            return;
        }
        if (imported->empty()) {
            emit changed();
            return;
        }
        opening_ = true;
        emit changed();
        catalog().fetchMissing(*imported, this, [this, imported, targetX, targetY, onAdded](bool ok) {
            opening_ = false;
            if (!ok) {
                emit notice("Unable to import pipeline: it uses an element this installation does not have.");
                emit changed();
                return;
            }
            const bool wasEmpty = plan().empty();
            const QPointF anchor = (std::isfinite(targetX) && std::isfinite(targetY))
                                       ? QPointF(targetX, targetY)
                                       : (wasEmpty ? QPointF(40, 40) : views_.freeSpot());
            const QStringList created = pasteSubplan(*imported, anchor.x(), anchor.y());
            emit changed();
            if (!created.isEmpty()) {
                if (wasEmpty) layout_->align();
                else layout_->align(created, CanvasLayout::Pin{created, anchor});
                if (onAdded) onAdded(created);
            }
        });
    });
}

// Saving ends every template edit; the target writes the plan, and the document is bound to it, saved there. A target
// that counts revisions (a library template) gives the document the revision it wrote.
SaveResult Controller::write(std::shared_ptr<SaveTarget> target) {
    if (const QString why = saveBlockedReason(); !why.isEmpty()) return SaveResult::failed("Unable to save: " + why);
    templates_.commitAll();
    gps::Graph document = plan();
    document.complete = result().complete();
    const SaveResult r = target->write(document);
    if (!r.ok && r.path.isEmpty()) return r;
    target_ = std::move(target);
    if (!r.ok) {
        // The conf on disk now matches the graph. Keep the edit state dirty so metadata can be saved again.
        emit synced();
        emit changed();
        return r;
    }
    if (r.revision > 0 && r.revision != plan().revision) {
        edits_.transact([&](gps::Graph& g, Edit& edit) {
            g.revision = r.revision;
            edit.action = QString("Saved %1, revision %2").arg(target_->title()).arg(r.revision);
            edit.presentation = true;
            return true;
        });
    }
    edits_.markSaved();
    emit changed();
    return r;
}
