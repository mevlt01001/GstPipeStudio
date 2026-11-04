// gst_engine.h – GstEngine: runs GStreamer 1.16+ native validation on tracked chains of a Pipeline/Plan,
// reads live negotiated caps and Sometimes pads, and updates the Pipeline model.
#pragma once

#include "builder.h"
#include "gst_element_creator.h"
#include "pipeline.h"

namespace gps {

class GstEngine {
public:
    // Validates only the tracked chains (elements fed from a SRC element) of a Plan.
    // Unlinked / unfed Filter and Sink elements are ignored instead of stalling GStreamer.
    static UpdateResult validateTracked(const Plan& plan, const UpdateOptions& options = {}, const LogFn& log = {});

    // Runs validation directly on an OOP Pipeline instance, updating its live Pad::Caps and Sometimes pads.
    static UpdateResult run(Pipeline* pipeline, const UpdateOptions& options = {}, const LogFn& log = {});
};

}  // namespace gps

