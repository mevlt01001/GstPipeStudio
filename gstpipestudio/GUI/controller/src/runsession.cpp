// runsession.cpp – see runsession.h.
#include "runsession.h"

#include "graph.h"
#include "helperclient.h"

namespace {
constexpr int kMonitorKillMs = 2000;
}

RunSession::RunSession(QObject* parent) : QObject(parent) {}

bool RunSession::run(const gps::Graph& plan) { return start(Mode::Run, plan); }

bool RunSession::startMonitoring(const gps::Graph& plan) { return start(Mode::Monitor, plan); }

void RunSession::stop() {
    if (run_) run_->interrupt();
}

void RunSession::stopMonitoring() {
    if (monitor_) monitor_->interrupt(kMonitorKillMs);
}

// The complete plan in a temporary file, then its helper on it; "" (and a notice) when it cannot be written.
bool RunSession::start(Mode mode, const gps::Graph& plan) {
    const bool monitor = mode == Mode::Monitor;
    gps::Graph copy = plan;
    copy.complete = true;
    const QString file = tmp_.filePath(monitor ? "monitor.conf" : "run.conf");
    std::string error;
    if (!copy.save(file.toStdString(), &error)) {
        emit notice("Unable to write the temporary plan file: " + QString::fromStdString(error));
        return false;
    }
    if (!monitor) emit logLine("Running…");
    auto log = [this](const QString& line) { emit logLine(line); };
    auto done = [this, mode](const HelperResult& r) { finished(mode, r); };
    if (monitor) monitor_ = HelperClient::run(this, "gps-monitor", {file}, 0, log, done, [this](const QString& line) { sample(line); });
    else run_ = HelperClient::run(this, "gps-run", {file}, 0, log, done);
    emit changed();
    return true;
}

void RunSession::sample(const QString& line) {
    gps::TelemetrySample sample;
    if (gps::fromJson(line.toStdString(), &sample)) emit telemetryReceived(sample);
}

void RunSession::finished(Mode mode, const HelperResult& r) {
    const bool monitor = mode == Mode::Monitor;
    (monitor ? monitor_ : run_) = nullptr;
    gps::RunResult answer;
    answer.status.clear();
    const bool answered = r.read(&answer) && !answer.status.empty();
    QString status;
    if (r.outcome == HelperResult::Finished && answered) status = QString::fromStdString(answer.status);
    else if (r.outcome == HelperResult::Crashed) status = "crashed";
    else if (r.outcome == HelperResult::NotFound) status = "not-found";
    else status = "error";
    const QString message = answered ? QString::fromStdString(answer.message) : QString();
    if (r.outcome == HelperResult::NotFound) emit notice(QString(monitor ? "gps-monitor" : "gps-run") + " was not found");
    emit logLine((monitor ? "Monitor finished: " : "Run finished: ") + status + (message.isEmpty() ? "" : " – " + message));
    emit changed();
    if (!monitor) emit runFinished(status);
}
