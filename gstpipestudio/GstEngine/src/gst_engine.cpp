#include "gst_engine.h"

namespace gps {
void GstEngine::execute(const ExecutionPlan& plan, Completion done) {
    cancelled_ = false;
    UpdateOptions options = options_;
    auto externalStop = options.shouldStop;
    options.shouldStop = [this, externalStop] { return cancelled_ || (externalStop && externalStop()); };
    std::string error;
    UpdateResult result;
    if (!plan.graph().validate(&error)) {
        result.status = "error"; result.message = error;
    } else if (!plan.graph().empty()) {
        result = update(plan.graph(), options, log_);
    }
    if (cancelled_) result.status = "cancelled";
    done({plan.ticket(), std::move(result)});
}
} // namespace gps
