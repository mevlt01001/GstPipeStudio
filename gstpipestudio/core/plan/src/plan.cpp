// plan.cpp – the rules of SPEC 4. Nothing in here knows an element by name except the queue (SPEC 4.4).
#include "plan.h"

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

const PlanElement* Plan::find(const std::string& name) const {
    for (const PlanElement& e : elements) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

PlanElement* Plan::find(const std::string& name) {
    return const_cast<PlanElement*>(static_cast<const Plan*>(this)->find(name));
}

int Plan::linkFrom(const std::string& srcElement, const std::string& srcPad) const {
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].srcElement == srcElement && links[i].srcPad == srcPad) return static_cast<int>(i);
    }
    return -1;
}

std::vector<int> Plan::linksFromElement(const std::string& srcElement) const {
    std::vector<int> found;
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].srcElement == srcElement) found.push_back(static_cast<int>(i));
    }
    return found;
}

std::vector<int> Plan::linksInto(const std::string& sinkElement) const {
    std::vector<int> found;
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].sinkElement == sinkElement) found.push_back(static_cast<int>(i));
    }
    return found;
}

bool Plan::sinkPadTaken(const std::string& sinkElement, const std::string& sinkPad) const {
    return std::any_of(links.begin(), links.end(), [&](const PlanLink& l) {
        return l.sinkElement == sinkElement && l.sinkPad == sinkPad;
    });
}

bool Plan::wouldCycle(const std::string& srcElement, const std::string& sinkElement) const {
    if (srcElement == sinkElement) return true;
    // A cycle appears if the sink element already leads back to the src element.
    std::vector<std::string> todo = {sinkElement};
    std::set<std::string> seen;
    while (!todo.empty()) {
        std::string current = todo.back();
        todo.pop_back();
        if (!seen.insert(current).second) continue;
        for (const PlanLink& l : links) {
            if (l.srcElement != current) continue;
            if (l.sinkElement == srcElement) return true;
            todo.push_back(l.sinkElement);
        }
    }
    return false;
}

std::string Plan::ouroborosMessage(const std::string& srcElement, const std::string& sinkElement) {
    if (srcElement == sinkElement) {
        return "Ouroboros!: You cannot link '" + srcElement + "' back into itself — an element cannot eat its own tail!";
    }
    return "Ouroboros!: Linking '" + srcElement + "' into '" + sinkElement + "' creates a closed loop — pipelines cannot eat their own tail!";
}

std::vector<std::string> Plan::trackedElements(const TraitsFn& traits) const {
    std::set<std::string> active;
    std::vector<std::string> queue;
    for (const PlanElement& e : elements) {
        if (traits && traits(e.factory).isSource) {
            active.insert(e.name);
            queue.push_back(e.name);
        }
    }
    while (!queue.empty()) {
        std::string current = queue.back();
        queue.pop_back();
        for (const PlanLink& l : links) {
            if (l.srcElement == current && active.insert(l.sinkElement).second) {
                queue.push_back(l.sinkElement);
            }
        }
    }
    std::vector<std::string> ordered;
    for (const PlanElement& e : elements) {
        if (active.count(e.name)) ordered.push_back(e.name);
    }
    return ordered;
}

bool Plan::isElementTracked(const std::string& name, const TraitsFn& traits) const {
    std::vector<std::string> tracked = trackedElements(traits);
    return std::find(tracked.begin(), tracked.end(), name) != tracked.end();
}

Plan Plan::trackedSubplan(const TraitsFn& traits) const {
    std::vector<std::string> tracked = trackedElements(traits);
    std::set<std::string> keep(tracked.begin(), tracked.end());
    Plan sub;
    sub.name = name;
    sub.nextId = nextId;
    for (const PlanElement& e : elements) {
        if (keep.count(e.name)) sub.elements.push_back(e);
    }
    for (const PlanLink& l : links) {
        if (keep.count(l.srcElement) && keep.count(l.sinkElement)) sub.links.push_back(l);
    }
    for (const ExposedParam& p : exposed) {
        if (keep.count(p.element)) sub.exposed.push_back(p);
    }
    return sub;
}

// ---- additions

std::string Plan::newName(const std::string& factory) {
    std::string base;
    for (char c : factory) base += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return base + "_" + std::to_string(nextId++);
}

std::string Plan::addElement(const std::string& factory, const Props& props, double x, double y) {
    return addSource(factory, props, x, y);
}

std::string Plan::addSource(const std::string& factory, const Props& props, double x, double y) {
    PlanElement e;
    e.name = newName(factory);
    e.factory = factory;
    e.props = props;
    e.x = x;
    e.y = y;
    elements.push_back(e);
    return e.name;
}

std::string Plan::addAfter(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                           const std::string& sinkPad, const Props& props, double x, double y) {
    if (!find(srcElement) || linkFrom(srcElement, srcPad) >= 0) return "";

    PlanElement e;
    e.name = newName(factory);
    e.factory = factory;
    e.props = props;
    e.x = x;
    e.y = y;
    PlanLink l;
    l.srcElement = srcElement;
    l.srcPad = srcPad;
    l.sinkElement = e.name;
    l.sinkPad = sinkPad;
    elements.push_back(e);
    links.push_back(l);
    return e.name;
}

bool Plan::linkExisting(const std::string& srcElement, const std::string& srcPad, const std::string& sinkElement,
                        const std::string& sinkPad, const std::string& caps, const Props& sinkPadProps) {
    if (!find(srcElement) || !find(sinkElement) || linkFrom(srcElement, srcPad) >= 0) return false;
    if (!isTemplateName(sinkPad) && sinkPadTaken(sinkElement, sinkPad)) return false;
    if (wouldCycle(srcElement, sinkElement)) return false;

    PlanLink l;
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

bool Plan::setProps(const std::string& element, const Props& props) {
    PlanElement* e = find(element);
    if (!e) return false;
    e->props = props;
    return true;
}

bool Plan::setCaps(const std::string& srcElement, const std::string& srcPad, const std::string& caps) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links[i].caps = caps;
    return true;
}

bool Plan::setSinkPadProps(const std::string& srcElement, const std::string& srcPad, const Props& props) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links[i].sinkPadProps = props;
    return true;
}

bool Plan::setRequestSrc(const std::string& element, const std::string& templateName, int count) {
    PlanElement* e = find(element);
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

bool Plan::setRequestSink(const std::string& element, const std::string& templateName, int count) {
    PlanElement* e = find(element);
    if (!e || count < 0) return false;
    int linkedCount = 0;
    for (const PlanLink& l : links) {
        if (l.sinkElement == element && l.sinkPad == templateName) ++linkedCount;
    }
    if (count < linkedCount) return false;
    if (count == 0) e->requestSink.erase(templateName);
    else e->requestSink[templateName] = count;
    return true;
}

void Plan::setExposed(const std::vector<ExposedParam>& params) {
    exposed.clear();
    for (const ExposedParam& p : params) {
        if (find(p.element) && !p.name.empty()) exposed.push_back(p);
    }
}

bool Plan::disconnectLink(const std::string& srcElement, const std::string& srcPad) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return false;
    links.erase(links.begin() + i);
    return true;
}

bool Plan::deleteElement(const std::string& name, bool autoHeal, int totalSrcPads) {
    if (!find(name)) return false;
    std::vector<int> in = linksInto(name);
    std::vector<int> out = linksFromElement(name);

    // Auto-heal only when there is unambiguously 1 incoming link and 1 outgoing link (and not a multi-src element).
    bool canHeal = autoHeal && in.size() == 1 && out.size() == 1 && totalSrcPads <= 1;
    PlanLink healed;
    if (canHeal) {
        const PlanLink& inLink = links[in[0]];
        const PlanLink& outLink = links[out[0]];
        healed = inLink;
        healed.sinkElement = outLink.sinkElement;
        healed.sinkPad = outLink.sinkPad;
        healed.sinkPadProps = outLink.sinkPadProps;
        if (healed.caps.empty()) healed.caps = outLink.caps;
    }

    Removal removed;
    dropElement(name, &removed);

    if (canHeal && !wouldCycle(healed.srcElement, healed.sinkElement)) {
        if (isTemplateName(healed.sinkPad) || !sinkPadTaken(healed.sinkElement, healed.sinkPad)) {
            links.push_back(healed);
        }
    }
    return true;
}

bool Plan::spliceElement(const std::string& elementName, const std::string& linkSrcElement, const std::string& linkSrcPad,
                         const std::vector<std::string>& freeSinkPads, const std::vector<std::string>& freeSrcPads) {
    if (!find(elementName)) return false;
    int idx = linkFrom(linkSrcElement, linkSrcPad);
    if (idx < 0) return false;
    PlanLink original = links[idx];
    if (original.srcElement == elementName || original.sinkElement == elementName) return false;

    // Ambiguous pad counts are not auto-spliced.
    if (freeSinkPads.size() > 1 || freeSrcPads.size() > 1) return false;
    if (freeSinkPads.empty() && freeSrcPads.empty()) return false;

    if (freeSinkPads.size() == 1 && freeSrcPads.size() == 1) {
        if (wouldCycle(original.srcElement, elementName) || wouldCycle(elementName, original.sinkElement)) return false;
        PlanLink in = original;
        in.sinkElement = elementName;
        in.sinkPad = freeSinkPads[0];
        in.sinkPadProps.clear();

        PlanLink out;
        out.srcElement = elementName;
        out.srcPad = freeSrcPads[0];
        out.sinkElement = original.sinkElement;
        out.sinkPad = original.sinkPad;
        out.sinkPadProps = original.sinkPadProps;

        links[idx] = in;
        links.insert(links.begin() + idx + 1, out);
        return true;
    }
    if (freeSinkPads.size() == 1 && freeSrcPads.empty()) {
        if (wouldCycle(original.srcElement, elementName)) return false;
        links[idx].sinkElement = elementName;
        links[idx].sinkPad = freeSinkPads[0];
        links[idx].sinkPadProps.clear();
        return true;
    }
    if (freeSinkPads.empty() && freeSrcPads.size() == 1) {
        if (wouldCycle(elementName, original.sinkElement)) return false;
        links[idx].srcElement = elementName;
        links[idx].srcPad = freeSrcPads[0];
        return true;
    }
    return false;
}

// ---- removals

void Plan::dropElement(const std::string& name, Removal* removed) {
    elements.erase(std::remove_if(elements.begin(), elements.end(), [&](const PlanElement& e) { return e.name == name; }),
                   elements.end());
    size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const PlanLink& l) { return l.srcElement == name || l.sinkElement == name; }),
                links.end());
    removed->links += static_cast<int>(before - links.size());
    removed->elements.push_back(name);
    exposed.erase(std::remove_if(exposed.begin(), exposed.end(), [&](const ExposedParam& p) { return p.element == name; }),
                  exposed.end());
}

Removal Plan::prune(const TraitsFn& traits) {
    Removal removed;
    for (;;) {
        std::vector<std::string> dead;
        for (const PlanElement& e : elements) {
            if (!traits(e.factory).isSource && linksInto(e.name).empty()) dead.push_back(e.name);
        }
        if (dead.empty()) return removed;
        for (const std::string& name : dead) dropElement(name, &removed);
    }
}

Removal Plan::cut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) {
    Removal removed;
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return removed;

    links.erase(links.begin() + i);
    removed.links = 1;
    Removal pruned = prune(traits);
    removed.elements = pruned.elements;
    removed.links += pruned.links;
    return removed;
}

Removal Plan::previewCut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) const {
    Plan copy = *this;
    return copy.cut(srcElement, srcPad, traits);
}

Addition Plan::replace(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                       const std::string& sinkPad, const Props& props, const TraitsFn& traits) {
    Addition result;
    // The new element takes the place of the one it replaces.
    double x = 0, y = 0;
    int old = linkFrom(srcElement, srcPad);
    if (old >= 0) {
        if (const PlanElement* replaced = find(links[old].sinkElement)) {
            x = replaced->x;
            y = replaced->y;
        }
    }
    result.removed = cut(srcElement, srcPad, traits);
    if (result.removed.links == 0) return result;  // the pad was free, there was nothing to replace
    result.element = addAfter(srcElement, srcPad, factory, sinkPad, props, x, y);
    return result;
}

Removal Plan::previewReplace(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) const {
    return previewCut(srcElement, srcPad, traits);
}

Removal Plan::deleteSource(const std::string& name, const TraitsFn& traits) {
    Removal removed;
    const PlanElement* e = find(name);
    if (!e || !traits(e->factory).isSource) return removed;

    dropElement(name, &removed);
    Removal pruned = prune(traits);
    removed.elements.insert(removed.elements.end(), pruned.elements.begin(), pruned.elements.end());
    removed.links += pruned.links;
    return removed;
}

Removal Plan::previewDeleteSource(const std::string& name, const TraitsFn& traits) const {
    Plan copy = *this;
    return copy.deleteSource(name, traits);
}

// ---- the queue shortcut. Its pad names are part of the exception (SPEC 4.4).

std::string Plan::insertQueue(const std::string& srcElement, const std::string& srcPad) {
    int i = linkFrom(srcElement, srcPad);
    if (i < 0) return "";

    PlanLink original = links[i];
    const PlanElement* src = find(srcElement);
    const PlanElement* sink = find(original.sinkElement);
    PlanElement q;
    q.name = newName(kQueueFactory);
    q.factory = kQueueFactory;
    q.x = (src->x + sink->x) / 2;
    q.y = (src->y + sink->y) / 2;

    // The caps choice stays on the first link. The sink pad settings move to the second.
    // The second link takes the old link's place in the list, so a Request sink keeps its pad order.
    PlanLink in = original;
    in.sinkElement = q.name;
    in.sinkPad = kQueueSinkPad;
    in.sinkPadProps.clear();
    PlanLink out;
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

bool Plan::removeQueue(const std::string& queueName) {
    const PlanElement* q = find(queueName);
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

    PlanLink merged = links[inIndex];
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
