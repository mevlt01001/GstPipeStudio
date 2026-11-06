#include "executionbackend.h"
#include "updateresult.h"
#include <QFile>
#include <QTimer>

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
        UpdateResult report;
        switch (result.outcome) {
        case HelperResult::Cancelled: report = UpdateResult::failure("cancelled", "Validation cancelled", {}); break;
        case HelperResult::Crashed: report = UpdateResult::failure("crashed", QString("Validator had to stop unexpectedly (signal %1)").arg(result.exitCode), {}); break;
        case HelperResult::TimedOut: report = UpdateResult::failure("hung", "Validator did not answer in time", {}); break;
        case HelperResult::NotFound: report = UpdateResult::failure("error", "gps-update was not found", {}); break;
        default:
            report = result.hasJson && result.json["status"].isString()
                ? UpdateResult::fromJson(result.json)
                : UpdateResult::failure("stopped", QString("Validator stopped unexpectedly (exit code %1)").arg(result.exitCode), {});
            break;
        }
        QPointer<HelperExecutionBackend> alive(this);
        done({ticket, report.toCore()});
        if (!alive) return;
        if (pending_) {
            auto next = std::move(*pending_);
            pending_.reset();
            execute(next.plan, std::move(next.done));
        }
    });
    client->start(findHelper("gps-update"), {path, "--timeout-ms", QString::number(timeoutMs_)}, timeoutMs_ + 2000);
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
