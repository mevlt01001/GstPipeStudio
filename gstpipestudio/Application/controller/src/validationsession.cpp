// validationsession.cpp – see validationsession.h.
#include "validationsession.h"

#include "contract.h"
#include "slowwatch.h"

ValidationSession::ValidationSession(gps::Pipeline& pipeline, QObject* parent)
    : QObject(parent), pipeline_(pipeline), watch_(new SlowWatch(this)) {
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("GPS_UPDATE_TIMEOUT_MS", &ok);
    if (ok && ms > 0) timeoutMs_ = ms;
    pause_.setSingleShot(true);
    pause_.setInterval(35);
    connect(&pause_, &QTimer::timeout, this, &ValidationSession::dispatch);
    connect(&backend_, &HelperExecutionBackend::logLine, this, [this](const QString& line) { emit logLine("[gps-update] " + line); });
    connect(watch_, &SlowWatch::slow, this, [this] {
        emit logLine("Validation is taking longer than expected");
        emit changed();
    });
}

bool ValidationSession::slowAsked() const { return updating_ && watch_->asking(); }

void ValidationSession::request(const QString& suspect) {
    suspect_ = suspect;
    execution_.supersede();
    pause_.start();
    cancelled_ = false;
    updating_ = true;
    watch_->start();
    emit changed();
}

// Nothing fed by a source runs: there is nothing to ask gps-update about.
void ValidationSession::dispatch() {
    cancelled_ = false;
    const gps::Graph& plan = pipeline_.editor().view();
    if (pipeline_.executed(plan).empty()) {
        pipeline_.observeIdle();
        stopped(false);
        emit finished(true);
        return;
    }
    updating_ = true;
    watch_->start();
    emit changed();
    backend_.setTimeout(timeoutMs_);
    backend_.setSuspect(suspect_.toStdString());
    execution_.start([this](const gps::ExecutionResult& result) { finish(result); });
}

// Still waiting for the pause: nothing to stop. Running: the validator is stopped and answers "cancelled".
void ValidationSession::cancel() {
    if (!updating_ || cancelling_) return;
    if (pause_.isActive()) {
        pause_.stop();
        execution_.supersede();
        stopped(true);
        emit finished(false);
        return;
    }
    cancelling_ = true;
    emit logLine("Validation cancelled: stopping the validator");
    execution_.cancel();
    emit changed();
}

void ValidationSession::waitMore() {
    watch_->extend();
    emit changed();
}

void ValidationSession::finish(const gps::ExecutionResult& r) {
    const UpdateResult& result = pipeline_.observations();
    const bool cancelled = r.report.status == "cancelled";
    emit logLine(cancelled ? "Validation cancelled — press Validate to run it again"
                           : "Validation: " + statusText(result) + (result.message.empty() ? QString() : " – " + QString::fromStdString(result.message)));
    stopped(cancelled);
    emit finished(!cancelled && result.ok());
}

void ValidationSession::stopped(bool cancelled) {
    updating_ = false;
    cancelling_ = false;
    cancelled_ = cancelled;
    watch_->stop();
    emit changed();
}
