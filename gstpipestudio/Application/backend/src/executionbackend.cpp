#include "executionbackend.h"
#include "contract.h"
#include <QFile>
#include <QTimer>
#include <algorithm>
#include <limits>

void HelperExecutionBackend::execute(const gps::ExecutionPlan& plan, Completion done) {
    if (client_) {
        pending_ = Pending{plan, std::move(done)};
        client_->cancel();
        return;
    }
    const auto ticket = plan.ticket();
    const QString path = temporary_.filePath(QString("revision-%1-run-%2.conf").arg(ticket.revision).arg(ticket.run));
    std::string error;
    if (!plan.graph().save(toStd(path), &error)) {
        gps::UpdateResult report; report.status = "error"; report.message = error;
        QTimer::singleShot(0, this, [done, ticket, report] { done({ticket, report}); });
        return;
    }
    auto* client = new HelperClient(this);
    client_ = client;
    connect(client, &HelperClient::stderrLine, this, &HelperExecutionBackend::logLine);
    connect(client, &HelperClient::finished, this, [this, client, done, ticket, path](const HelperResult& result) {
        if (client_ == client) client_ = nullptr;
        client->deleteLater();
        QFile::remove(path);
        using gps::UpdateResult;
        UpdateResult report;
        report.status.clear();
        switch (result.outcome) {
        case HelperResult::Cancelled: report = UpdateResult::failure("cancelled", "Validation cancelled"); break;
        case HelperResult::Crashed: report = UpdateResult::failure("crashed", "Validator had to stop unexpectedly (signal " + std::to_string(result.exitCode) + ")"); break;
        case HelperResult::TimedOut: report = UpdateResult::failure("hung", "Validator did not answer in time"); break;
        case HelperResult::NotFound: report = UpdateResult::failure("error", "gps-update was not found"); break;
        default:
            if (!result.read(&report) || !report.valid())
                report = UpdateResult::failure("stopped", "Validator stopped unexpectedly (exit code " + std::to_string(result.exitCode) + ")");
            break;
        }
        if ((report.status == "crashed" || report.status == "hung") && report.errorElement.empty() && !suspect_.empty()) {
            report.errorElement = suspect_;
            report.message = "Element " + suspect_ + ": " + report.message;
        }
        QPointer<HelperExecutionBackend> alive(this);
        done({ticket, report});
        if (!alive) return;
        if (pending_) {
            auto next = std::move(*pending_);
            pending_.reset();
            execute(next.plan, std::move(next.done));
        }
    });
    constexpr int exitGraceMs = 2000;
    const int watchdogMs = std::min(timeoutMs_, std::numeric_limits<int>::max() - exitGraceMs) + exitGraceMs;
    client->start(findHelper("gps-update"), {path, "--timeout-ms", QString::number(timeoutMs_)}, watchdogMs);
}
void HelperExecutionBackend::cancel() {
    if (pending_) {
        auto pending = std::move(*pending_);
        pending_.reset();
        gps::UpdateResult report; report.status = "cancelled";
        const auto ticket = pending.plan.ticket();
        QTimer::singleShot(0, this, [done = std::move(pending.done), ticket, report] { done({ticket, report}); });
    }
    if (client_) client_->cancel();
}
