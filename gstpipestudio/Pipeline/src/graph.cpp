// plan.cpp – the rules of SPEC 4. Nothing in here knows an element by name except the queue (SPEC 4.4).
#include "graph.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace gps {

std::string padIdentity(const std::string& templateName, int index) {
    return templateName + "#" + std::to_string(index);
}

bool splitPadIdentity(const std::string& identity, std::string* templateName, int* index) {
    size_t hash = identity.rfind('#');
    if (hash == std::string::npos || hash == 0 || hash + 1 >= identity.size() || identity.size() - hash > 9) return false;
    for (size_t i = hash + 1; i < identity.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(identity[i]))) return false;
    }
    if (templateName) *templateName = identity.substr(0, hash);
    if (index) *index = std::stoi(identity.substr(hash + 1));
    return true;
}

bool isTemplateName(const std::string& padName) {
    return padName.find('%') != std::string::npos;
}

std::map<std::string, int> defaultRequestSrc(const std::vector<std::pair<std::string, std::string>>& srcTemplates) {
    std::map<std::string, int> counts;
    if (srcTemplates.empty()) return counts;
    bool allRequest = std::all_of(srcTemplates.begin(), srcTemplates.end(),
                                  [](const auto& t) { return t.second == "request"; });
    if (allRequest) counts[srcTemplates.front().first] = 1;
    return counts;
}

// ---- lookup

const Element* Graph::find(const std::string& name) const {
    for (const Element& e : elements) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

Element* Graph::find(const std::string& name) {
    return const_cast<Element*>(static_cast<const Graph*>(this)->find(name));
}

std::string Graph::sinkIdentity(size_t index) const {
    const auto& link = links.at(index);
    if (!isTemplateName(link.sinkPad)) return link.sinkPad;
    int ordinal = 0;
    for (size_t i = 0; i < index; ++i)
        if (links[i].sinkElement == link.sinkElement && links[i].sinkPad == link.sinkPad) ++ordinal;
    return padIdentity(link.sinkPad, ordinal);
}

int Graph::linkFrom(const std::string& srcElement, const std::string& srcPad) const {
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].srcElement == srcElement && links[i].srcPad == srcPad) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> Graph::linksFromElement(const std::string& srcElement) const {
    std::vector<int> found;
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].srcElement == srcElement) found.push_back(static_cast<int>(i));
    }
    return found;
}

std::vector<int> Graph::linksInto(const std::string& sinkElement) const {
    std::vector<int> found;
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].sinkElement == sinkElement) found.push_back(static_cast<int>(i));
    }
    return found;
}

bool Graph::sinkPadTaken(const std::string& sinkElement, const std::string& sinkPad) const {
    return std::any_of(links.begin(), links.end(), [&](const Connection& l) {
        return l.sinkElement == sinkElement && l.sinkPad == sinkPad;
    });
}

bool Graph::wouldCycle(const std::string& srcElement, const std::string& sinkElement) const {
    if (srcElement == sinkElement) return true;
    // A cycle appears if the sink element already leads back to the src element.
    std::vector<std::string> todo = {sinkElement};
    std::set<std::string> seen;
    while (!todo.empty()) {
        std::string current = todo.back();
        todo.pop_back();
        if (!seen.insert(current).second) continue;
        for (const Connection& l : links) {
            if (l.srcElement != current) continue;
            if (l.sinkElement == srcElement) return true;
            todo.push_back(l.sinkElement);
        }
    }
    return false;
}

std::string Graph::ouroborosMessage(const std::string& srcElement, const std::string& sinkElement) {
    if (srcElement == sinkElement) {
        return "Ouroboros!: You cannot link '" + srcElement + "' back into itself — an element cannot eat its own tail!";
    }
    return "Ouroboros!: Linking '" + srcElement + "' into '" + sinkElement + "' creates a closed loop — pipelines cannot eat their own tail!";
}

std::vector<std::string> Graph::trackedElements(const TraitsFn& traits) const {
    std::set<std::string> active;
    std::vector<std::string> queue;
    for (const Element& e : elements) {
        if (traits && traits(e.factory).isSource) {
            active.insert(e.name);
            queue.push_back(e.name);
        }
    }
    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();
        for (const Connection& l : links) {
            if (l.srcElement == current && active.insert(l.sinkElement).second) {
                queue.push_back(l.sinkElement);
            }
        }
    }
    std::vector<std::string> ordered;
    for (const Element& e : elements) {
        if (active.count(e.name)) ordered.push_back(e.name);
    }
    return ordered;
}

bool Graph::isElementTracked(const std::string& name, const TraitsFn& traits) const {
    std::vector<std::string> tracked = trackedElements(traits);
    return std::find(tracked.begin(), tracked.end(), name) != tracked.end();
}

Graph Graph::trackedSubplan(const TraitsFn& traits) const {
    std::vector<std::string> tracked = trackedElements(traits);
    std::set<std::string> keep(tracked.begin(), tracked.end());
    Graph sub;
    sub.name = name;
    sub.nextId = nextId;
    for (const Element& e : elements) {
        if (keep.count(e.name)) sub.elements.push_back(e);
    }
    for (const Connection& l : links) {
        if (keep.count(l.srcElement) && keep.count(l.sinkElement)) sub.links.push_back(l);
    }
    for (const ExposedParam& p : exposed) {
        if (keep.count(p.element)) sub.exposed.push_back(p);
    }
    return sub;
}

// ---- additions

std::string Graph::newName(const std::string& factory) {
    std::string base;
    for (char c : factory) base += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return base + "_" + std::to_string(nextId++);
}

std::string Graph::addElement(const std::string& factory, const Props& props, double x, double y) {
    return addSource(factory, props, x, y);
}

std::string Graph::addSource(const std::string& factory, const Props& props, double x, double y) {
    Element e;
    e.name = newName(factory);
    e.factory = factory;
    e.props = props;
    e.x = x;
    e.y = y;
    elements.push_back(e);
    return e.name;
}

std::string Graph::addAfter(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                           const std::string& sinkPad, const Props& props, double x, double y) {
    if (!find(srcElement) || linkFrom(srcElement, srcPad) >= 0) return "";

    Element e;
    e.name = newName(factory);
    e.factory = factory;
    e.props = props;
    e.x = x;
    e.y = y;
    Connection l;
    l.srcElement = srcElement;
    l.srcPad = srcPad;
    l.sinkElement = e.name;
    l.sinkPad = sinkPad;
    elements.push_back(e);
    links.push_back(l);
    return e.name;
}

bool Graph::linkExisting(const std::string& srcElement, const std::string& srcPad, const std::string& sinkElement,
                        const std::string& sinkPad, const std::string& caps, const Props& sinkPadProps) {
    if (!find(srcElement) || !find(sinkElement) || linkFrom(srcElement, srcPad) >= 0) return false;
    if (!isTemplateName(sinkPad) && sinkPadTaken(sinkElement, sinkPad)) return false;
    if (wouldCycle(srcElement, sinkElement)) return false;

    Connection l;
    l.srcElement = srcElement;
    l.srcPad = srcPad;
    l.sinkElement = sinkElement;
    l.sinkPad = sinkPad;
    l.caps = caps;
    l.sinkPadProps = sinkPadProps;
    links.push_back(l);
    return true;
}

// ---- edits

bool Graph::setProps(const std::string& element, const Props& props) {
    Element* e = find(element);
    if (!e) return false;
    e->props = props;
    return true;
}

bool Graph::setCaps(const std::string& srcElement, const std::string& srcPad, const std::string& caps) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links[i].caps = caps;
    return true;
}

bool Graph::setSinkPadProps(const std::string& srcElement, const std::string& srcPad, const Props& props) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links[i].sinkPadProps = props;
    return true;
}

bool Graph::setRequestSrc(const std::string& element, const std::string& templateName, int count) {
    Element* e = find(element);
    if (!e || count < 0) return false;

    std::vector<std::pair<int, int>> linked;  // (pad index, link index)
    for (size_t i = 0; i < links.size(); ++i) {
        std::string t;
        int k = 0;
        if (links[i].srcElement == element && splitPadIdentity(links[i].srcPad, &t, &k) && t == templateName) {
            linked.emplace_back(k, static_cast<int>(i));
        }
    }
    if (count < static_cast<int>(linked.size())) return false;

    std::sort(linked.begin(), linked.end());
    for (size_t j = 0; j < linked.size(); ++j) links[linked[j].second].srcPad = padIdentity(templateName, static_cast<int>(j));
    if (count == 0) {
        e->requestSrc.erase(templateName);
    } else {
        e->requestSrc[templateName] = count;
    }
    return true;
}

bool Graph::setRequestSink(const std::string& element, const std::string& templateName, int count) {
    Element* e = find(element);
    if (!e || count < 0) return false;
    int linkedCount = 0;
    for (const Connection& l : links) {
        if (l.sinkElement == element && l.sinkPad == templateName) ++linkedCount;
    }
    if (count < linkedCount) return false;
    if (count == 0) e->requestSink.erase(templateName);
    else e->requestSink[templateName] = count;
    return true;
}

void Graph::setExposed(const std::vector<ExposedParam>& params) {
    exposed.clear();
    for (const ExposedParam& p : params) {
        if (find(p.element) && !p.name.empty()) exposed.push_back(p);
    }
}

bool Graph::disconnectLink(const std::string& srcElement, const std::string& srcPad) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links.erase(links.begin() + i);
    return true;
}

bool Graph::deleteElements(const std::vector<std::string>& names, bool autoHeal, int exitTotalSrcPads) {
    std::set<std::string> group;
    for (const std::string& n : names) {
        if (find(n)) group.insert(n);
    }
    if (group.empty()) return false;

    std::vector<Connection> inLinks, outLinks;
    for (const Connection& l : links) {
        bool srcIn = group.count(l.srcElement) > 0;
        bool snkIn = group.count(l.sinkElement) > 0;
        if (!srcIn && snkIn) inLinks.push_back(l);
        else if (srcIn && !snkIn) outLinks.push_back(l);
    }

    // Auto-heal when the removed subgraph has unambiguously 1 external incoming link and 1 external outgoing link.
    bool canHeal = autoHeal && inLinks.size() == 1 && outLinks.size() == 1 && exitTotalSrcPads <= 1;
    Connection healed;
    if (canHeal) {
        healed = inLinks[0];
        healed.sinkElement = outLinks[0].sinkElement;
        healed.sinkPad = outLinks[0].sinkPad;
        healed.sinkPadProps = outLinks[0].sinkPadProps;
        if (healed.caps.empty()) healed.caps = outLinks[0].caps;
    }

    Removal removed;
    for (const std::string& n : names) {
        if (find(n)) dropElement(n, &removed);
    }

    if (canHeal && !wouldCycle(healed.srcElement, healed.sinkElement)) {
        if (isTemplateName(healed.sinkPad) || !sinkPadTaken(healed.sinkElement, healed.sinkPad)) {
            links.push_back(healed);
        }
    }
    return true;
}

bool Graph::deleteElement(const std::string& name, bool autoHeal, int totalSrcPads) {
    return deleteElements({name}, autoHeal, totalSrcPads);
}

bool Graph::spliceElement(const std::string& elementName, const std::string& linkSrcElement, const std::string& linkSrcPad,
                         const std::vector<std::string>& freeSinkPads, const std::vector<std::string>& freeSrcPads) {
    if (!find(elementName)) return false;
    std::vector<std::pair<std::string, std::string>> sinks, srcs;
    for (const std::string& p : freeSinkPads) sinks.emplace_back(elementName, p);
    for (const std::string& p : freeSrcPads) srcs.emplace_back(elementName, p);
    return spliceChain(linkSrcElement, linkSrcPad, sinks, srcs);
}

bool Graph::spliceChain(const std::string& linkSrcElement, const std::string& linkSrcPad,
                       const std::vector<std::pair<std::string, std::string>>& freeSinkPads,
                       const std::vector<std::pair<std::string, std::string>>& freeSrcPads) {
    int idx = linkFrom(linkSrcElement, linkSrcPad);
    if (idx < 0) return false;
    Connection original = links[idx];

    // Ambiguous pad counts are not auto-spliced.
    if (freeSinkPads.size() > 1 || freeSrcPads.size() > 1) return false;
    if (freeSinkPads.empty() && freeSrcPads.empty()) return false;

    if (freeSinkPads.size() == 1 && freeSrcPads.size() == 1) {
        const auto& [entryElem, entryPad] = freeSinkPads[0];
        const auto& [exitElem, exitPad] = freeSrcPads[0];
        if (!find(entryElem) || !find(exitElem)) return false;
        if (original.srcElement == entryElem || original.sinkElement == exitElem) return false;
        if (wouldCycle(original.srcElement, entryElem) || wouldCycle(exitElem, original.sinkElement)) return false;
        Connection in = original;
        in.sinkElement = entryElem;
        in.sinkPad = entryPad;
        in.sinkPadProps.clear();

        Connection out;
        out.srcElement = exitElem;
        out.srcPad = exitPad;
        out.sinkElement = original.sinkElement;
        out.sinkPad = original.sinkPad;
        out.sinkPadProps = original.sinkPadProps;

        links[idx] = in;
        links.insert(links.begin() + idx + 1, out);
        return true;
    }
    if (freeSinkPads.size() == 1 && freeSrcPads.empty()) {
        const auto& [entryElem, entryPad] = freeSinkPads[0];
        if (!find(entryElem) || original.srcElement == entryElem) return false;
        if (wouldCycle(original.srcElement, entryElem)) return false;
        links[idx].sinkElement = entryElem;
        links[idx].sinkPad = entryPad;
        links[idx].sinkPadProps.clear();
        return true;
    }
    if (freeSinkPads.empty() && freeSrcPads.size() == 1) {
        const auto& [exitElem, exitPad] = freeSrcPads[0];
        if (!find(exitElem) || original.sinkElement == exitElem) return false;
        if (wouldCycle(exitElem, original.sinkElement)) return false;
        links[idx].srcElement = exitElem;
        links[idx].srcPad = exitPad;
        return true;
    }
    return false;
}

// ---- removals

void Graph::dropElement(const std::string& name, Removal* removed) {
    elements.erase(std::remove_if(elements.begin(), elements.end(), [&](const Element& e) { return e.name == name; }),
                   elements.end());
    size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const Connection& l) { return l.srcElement == name || l.sinkElement == name; }),
                links.end());
    removed->links += static_cast<int>(before - links.size());
    removed->elements.push_back(name);
    exposed.erase(std::remove_if(exposed.begin(), exposed.end(), [&](const ExposedParam& p) { return p.element == name; }),
                  exposed.end());
}

Removal Graph::prune(const TraitsFn& traits) {
    Removal removed;
    for (;;) {
        std::vector<std::string> dead;
        for (const Element& e : elements) {
            if (!traits(e.factory).isSource && linksInto(e.name).empty()) dead.push_back(e.name);
        }
        if (dead.empty()) return removed;
        for (const std::string& name : dead) dropElement(name, &removed);
    }
}

Removal Graph::cut(const std::string& srcElement, const std::string& srcPad, const TraitsFn&) {
    Removal removed;
    if (disconnectLink(srcElement, srcPad)) removed.links = 1;
    return removed;
}

Removal Graph::previewCut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) const {
    Graph copy = *this;
    return copy.cut(srcElement, srcPad, traits);
}

Addition Graph::replace(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                       const std::string& sinkPad, const Props& props, const TraitsFn&) {
    Addition result;
    int old = linkFrom(srcElement, srcPad);
    if (old < 0) return result;
    std::string targetName = links[old].sinkElement;
    double x = 0, y = 0;
    if (const Element* replaced = find(targetName)) {
        x = replaced->x;
        y = replaced->y;
        dropElement(targetName, &result.removed);
    } else {
        disconnectLink(srcElement, srcPad);
        result.removed.links = 1;
    }
    result.element = addAfter(srcElement, srcPad, factory, sinkPad, props, x, y);
    return result;
}

Removal Graph::previewReplace(const std::string& srcElement, const std::string& srcPad, const TraitsFn&) const {
    Removal removed;
    int old = linkFrom(srcElement, srcPad);
    if (old >= 0) {
        Graph copy = *this;
        copy.dropElement(links[old].sinkElement, &removed);
    }
    return removed;
}

Removal Graph::deleteSource(const std::string& name, const TraitsFn& traits) {
    Removal removed;
    const Element* e = find(name);
    if (!e || (traits && !traits(e->factory).isSource)) return removed;
    dropElement(name, &removed);
    return removed;
}

Removal Graph::previewDeleteSource(const std::string& name, const TraitsFn& traits) const {
    Graph copy = *this;
    return copy.deleteSource(name, traits);
}

// ---- the queue shortcut. Its pad names are part of the exception (SPEC 4.4).

std::string Graph::insertQueue(const std::string& srcElement, const std::string& srcPad) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return "";

    Connection original = links[i];
    const Element* src = find(srcElement);
    const Element* sink = find(original.sinkElement);
    Element q;
    q.name = newName(kQueueFactory);
    q.factory = kQueueFactory;
    q.x = (src->x + sink->x) / 2;
    q.y = (src->y + sink->y) / 2;

    // The caps choice stays on the first link. The sink pad settings move to the second.
    // The second link takes the old link's place in the list, so a Request sink keeps its pad order.
    Connection in = original;
    in.sinkElement = q.name;
    in.sinkPad = kQueueSinkPad;
    in.sinkPadProps.clear();
    Connection out;
    out.srcElement = q.name;
    out.srcPad = kQueueSrcPad;
    out.sinkElement = original.sinkElement;
    out.sinkPad = original.sinkPad;
    out.sinkPadProps = original.sinkPadProps;

    elements.push_back(q);
    links[i] = in;
    links.insert(links.begin() + i + 1, out);
    return q.name;
}

bool Graph::removeQueue(const std::string& queueName) {
    const Element* q = find(queueName);
    if (!q || q->factory != kQueueFactory) return false;
    std::vector<int> in = linksInto(queueName);
    if (in.size() != 1) return false;

    int inIndex = in[0];
    int outIndex = linkFrom(queueName, kQueueSrcPad);
    Removal removed;
    if (outIndex < 0) {
        dropElement(queueName, &removed);  // a free output: the queue and its input link simply go
        return true;
    }

    Connection merged = links[inIndex];
    merged.sinkElement = links[outIndex].sinkElement;
    merged.sinkPad = links[outIndex].sinkPad;
    merged.sinkPadProps = links[outIndex].sinkPadProps;
    if (merged.caps.empty()) merged.caps = links[outIndex].caps;

    links[outIndex] = merged;
    links.erase(links.begin() + inIndex);
    dropElement(queueName, &removed);
    return true;
}

}  // namespace gps
