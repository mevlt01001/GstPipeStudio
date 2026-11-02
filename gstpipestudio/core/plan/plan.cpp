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

// ---- additions

std::string Plan::newName(const std::string& factory) {
    std::string base;
    for (char c : factory) base += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return base + "_" + std::to_string(nextId++);
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

void Plan::setExposed(const std::vector<ExposedParam>& params) {
    exposed.clear();
    for (const ExposedParam& p : params) {
        if (find(p.element) && !p.name.empty()) exposed.push_back(p);
    }
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
