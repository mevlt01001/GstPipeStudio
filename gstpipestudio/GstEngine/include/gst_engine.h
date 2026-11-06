#pragma once
#include "builder.h"
#include "gst_element_creator.h"
#include "pipeline.h"
#include <atomic>

namespace gps {
// Native backend: owns the run, never borrows the editable document. execute()
// completes on the calling thread. The GUI uses the process adapter instead.
class GstEngine final : public IExecutionBackend {
public:
    explicit GstEngine(UpdateOptions options = {}, LogFn log = {}) : options_(std::move(options)), log_(std::move(log)) {}
    void execute(const ExecutionPlan&, Completion) override;
    void cancel() override { cancelled_ = true; }
    static UpdateResult validateTracked(const Graph&, const UpdateOptions& = {}, const LogFn& = {});
private:
    UpdateOptions options_;
    LogFn log_;
    std::atomic<bool> cancelled_{false};
};
} // namespace gps
