// gst_engine.cpp – implementation of GstEngine.
#include "gst_engine.h"

namespace gps {

UpdateResult GstEngine::validateTracked(const Plan& plan, const UpdateOptions& options, const LogFn& log) {
    Plan tracked = plan.trackedSubplan(&GstElementCreator::queryFactoryTraits);
    if (tracked.empty()) {
        UpdateResult emptyResult;
        emptyResult.status = "ok";
        return emptyResult;
    }
    return update(tracked, options, log);
}

UpdateResult GstEngine::run(Pipeline* pipeline, const UpdateOptions& options, const LogFn& log) {
    if (!pipeline) {
        UpdateResult r;
        r.status = "error";
        r.message = "null pipeline";
        return r;
    }

    Plan plan;
    plan.name = pipeline->name();
    for (Element* el : pipeline->trackedElements()) {
        PlanElement pe;
        pe.name = el->name();
        pe.factory = el->factory();
        pe.props = el->props();
        pe.x = el->x();
        pe.y = el->y();
        for (Pad* p : el->srcPads()) {
            if (p->presence() == Pad::Presence::Request) {
                pe.requestSrc[p->templateName()]++;
            }
        }
        plan.elements.push_back(pe);
    }
    for (Element* el : pipeline->trackedElements()) {
        for (Pad* src : el->srcPads()) {
            if (!src->isLinked() || !src->linkedPad() || !src->linkedPad()->element()) continue;
            if (!src->linkedPad()->element()->isTracked()) continue;
            PlanLink l;
            l.srcElement = el->name();
            l.srcPad = src->name();
            l.sinkElement = src->linkedPad()->element()->name();
            l.sinkPad = src->linkedPad()->presence() == Pad::Presence::Request
                            ? src->linkedPad()->templateName()
                            : src->linkedPad()->name();
            l.caps = src->caps().userFilter;
            l.sinkPadProps = src->linkedPad()->padProps();
            plan.links.push_back(l);
        }
    }

    if (plan.empty()) {
        UpdateResult emptyResult;
        emptyResult.status = "ok";
        return emptyResult;
    }

    UpdateResult result = update(plan, options, log);
    for (const PadReport& r : result.pads) {
        Element* el = pipeline->findElement(r.element);
        if (!el) continue;
        Pad::Direction dir = r.isSrc ? Pad::Direction::Src : Pad::Direction::Sink;
        Pad::Presence pres = r.presence == "always"      ? Pad::Presence::Always
                             : r.presence == "request"   ? Pad::Presence::Request
                                                         : Pad::Presence::Sometimes;
        Pad* pad = el->findPad(r.identity, dir);
        if (!pad) {
            pad = el->addPad(r.identity, r.templateName, dir, pres);
        }
        pad->caps().currentCaps = r.caps;
        pad->caps().allowedCaps = r.allowed;
    }
    return result;
}

}  // namespace gps

