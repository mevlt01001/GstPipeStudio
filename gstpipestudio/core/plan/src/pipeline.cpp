// pipeline.cpp – implementation of the OOP Pipeline -> Element -> Pad -> Caps model.
#include "pipeline.h"

#include <algorithm>
#include <cctype>

namespace gps {

// ---- Pad

Pad::Pad(Element* owner, std::string name, std::string templateName, Direction dir, Presence presence,
         std::string templateCaps)
    : element_(owner),
      name_(std::move(name)),
      templateName_(std::move(templateName)),
      direction_(dir),
      presence_(presence) {
    caps_.templateCaps = std::move(templateCaps);
}

bool Pad::hasActiveFlow() const {
    if (!element_) return false;
    if (direction_ == Direction::Sink) {
        return linkedPad_ != nullptr && linkedPad_->element() != nullptr && linkedPad_->element()->isTracked();
    }
    return element_->isTracked();
}

// ---- Element

Element::Element(std::string name, std::string factory, Role role, Props props, double x, double y)
    : name_(std::move(name)),
      factory_(std::move(factory)),
      role_(role),
      props_(std::move(props)),
      x_(x),
      y_(y) {}

Pad* Element::addPad(const std::string& name, const std::string& templateName, Pad::Direction dir,
                     Pad::Presence presence, const std::string& templateCaps) {
    if (Pad* existing = findPad(name, dir)) return existing;
    ownedPads_.push_back(std::make_unique<Pad>(this, name, templateName, dir, presence, templateCaps));
    Pad* raw = ownedPads_.back().get();
    if (dir == Pad::Direction::Src) srcRaw_.push_back(raw);
    else sinkRaw_.push_back(raw);
    return raw;
}

Pad* Element::findPad(const std::string& name, Pad::Direction dir) const {
    const auto& list = dir == Pad::Direction::Src ? srcRaw_ : sinkRaw_;
    for (Pad* p : list) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

bool Element::isTracked() const {
    std::vector<const Element*> visited;
    return isTrackedVisited(&visited);
}

bool Element::isTrackedVisited(std::vector<const Element*>* visited) const {
    if (role_ == Role::Src) return true;
    if (std::find(visited->begin(), visited->end(), this) != visited->end()) return false;
    visited->push_back(this);
    for (const Pad* sink : sinkRaw_) {
        if (sink->isLinked() && sink->linkedPad()->element() &&
            sink->linkedPad()->element()->isTrackedVisited(visited)) {
            return true;
        }
    }
    return false;
}

bool Element::hasUnlinkedPads() const {
    for (const Pad* p : sinkRaw_) {
        if (!p->isLinked()) return true;
    }
    for (const Pad* p : srcRaw_) {
        if (!p->isLinked()) return true;
    }
    return false;
}

std::string Element::infoNote() const {
    if (!isTracked()) {
        return "Unfed sink pad: waiting for an active source connection (ignored during validation)";
    }
    if (hasUnlinkedPads()) {
        return "Has unlinked pad(s)";
    }
    return "";
}

// ---- Pipeline

Pipeline::Pipeline(std::string name) : name_(std::move(name)) {}

void Pipeline::run() {
    if (runner_) runner_(this);
}

std::vector<Element*> Pipeline::elements() const {
    std::vector<Element*> out;
    for (const auto& e : elements_) out.push_back(e.get());
    return out;
}

std::vector<Element*> Pipeline::srcElements() const {
    std::vector<Element*> out;
    for (const auto& e : elements_) {
        if (e->isSource()) out.push_back(e.get());
    }
    return out;
}

std::vector<Element*> Pipeline::trackedElements() const {
    std::vector<Element*> out;
    for (const auto& e : elements_) {
        if (e->isTracked()) out.push_back(e.get());
    }
    return out;
}

Element* Pipeline::findElement(const std::string& name) const {
    for (const auto& e : elements_) {
        if (e->name() == name) return e.get();
    }
    return nullptr;
}

Element* Pipeline::addElement(const std::string& factory, Element::Role role, const Props& props, double x, double y) {
    std::string base;
    for (char c : factory) base += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    std::string uniqueName = base + "_" + std::to_string(nextId_++);
    elements_.push_back(std::make_unique<Element>(uniqueName, factory, role, props, x, y));
    return elements_.back().get();
}

bool Pipeline::reaches(const Element* from, const Element* target) const {
    if (!from || !target) return false;
    if (from == target) return true;
    std::vector<const Element*> todo = {from};
    std::vector<const Element*> seen;
    while (!todo.empty()) {
        const Element* cur = todo.back();
        todo.pop_back();
        if (std::find(seen.begin(), seen.end(), cur) != seen.end()) continue;
        seen.push_back(cur);
        for (const Pad* src : cur->srcPads()) {
            if (!src->isLinked()) continue;
            const Element* next = src->linkedPad()->element();
            if (next == target) return true;
            if (next) todo.push_back(next);
        }
    }
    return false;
}

bool Pipeline::connect(Pad* srcPad, Pad* sinkPad, std::string* error) {
    if (!srcPad || !sinkPad || !srcPad->element() || !sinkPad->element()) {
        if (error) *error = "Invalid pad reference";
        return false;
    }
    if (srcPad->direction() == Pad::Direction::Sink && sinkPad->direction() == Pad::Direction::Src) {
        std::swap(srcPad, sinkPad);
    }
    if (!srcPad->isSrc() || !sinkPad->isSink()) {
        if (error) *error = "Links must connect a SRC pad to a SINK pad";
        return false;
    }
    if (!srcPad->element()->canLinkSrc() || !sinkPad->element()->canLinkSink()) {
        if (error) *error = "Element has no available pads on this side";
        return false;
    }
    if (reaches(sinkPad->element(), srcPad->element())) {
        if (error) *error = Plan::ouroborosMessage(srcPad->element()->name(), sinkPad->element()->name());
        return false;
    }
    if (srcPad->isLinked()) disconnect(srcPad);
    if (sinkPad->isLinked()) disconnect(sinkPad);

    srcPad->linkedPad_ = sinkPad;
    sinkPad->linkedPad_ = srcPad;
    run();
    return true;
}

bool Pipeline::disconnect(Pad* pad) {
    if (!pad || !pad->isLinked()) return false;
    Pad* peer = pad->linkedPad_;
    pad->linkedPad_ = nullptr;
    pad->caps_.userFilter.clear();
    if (peer) {
        peer->linkedPad_ = nullptr;
        peer->caps_.userFilter.clear();
    }
    run();
    return true;
}

bool Pipeline::setCaps(Pad* srcPad, const std::string& filterCaps) {
    if (!srcPad || !srcPad->isLinked()) return false;
    srcPad->caps_.userFilter = filterCaps;
    srcPad->linkedPad_->caps_.userFilter = filterCaps;
    run();
    return true;
}

bool Pipeline::removeElement(const std::string& name, bool autoHeal) {
    Element* target = findElement(name);
    if (!target) return false;

    std::vector<Pad*> linkedIn;
    for (Pad* p : target->sinkPads()) {
        if (p->isLinked()) linkedIn.push_back(p->linkedPad());
    }
    std::vector<Pad*> linkedOut;
    for (Pad* p : target->srcPads()) {
        if (p->isLinked()) linkedOut.push_back(p->linkedPad());
    }
    bool canHeal = autoHeal && linkedIn.size() == 1 && linkedOut.size() == 1 && target->srcPads().size() <= 1;
    std::string savedFilter;
    if (canHeal) {
        savedFilter = linkedIn[0]->caps().userFilter.empty() ? target->srcPads()[0]->caps().userFilter
                                                             : linkedIn[0]->caps().userFilter;
    }

    for (Pad* p : target->sinkPads()) {
        if (p->isLinked()) {
            p->linkedPad_->linkedPad_ = nullptr;
            p->linkedPad_ = nullptr;
        }
    }
    for (Pad* p : target->srcPads()) {
        if (p->isLinked()) {
            p->linkedPad_->linkedPad_ = nullptr;
            p->linkedPad_ = nullptr;
        }
    }

    elements_.erase(std::remove_if(elements_.begin(), elements_.end(),
                                   [&](const std::unique_ptr<Element>& e) { return e->name() == name; }),
                    elements_.end());

    if (canHeal && !reaches(linkedOut[0]->element(), linkedIn[0]->element())) {
        linkedIn[0]->linkedPad_ = linkedOut[0];
        linkedOut[0]->linkedPad_ = linkedIn[0];
        linkedIn[0]->caps().userFilter = savedFilter;
        linkedOut[0]->caps().userFilter = savedFilter;
    }
    run();
    return true;
}

}  // namespace gps
