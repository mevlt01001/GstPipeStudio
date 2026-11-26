// runsession.cpp – see runsession.h.
#include "runsession.h"

#include "graph.h"
#include "helperclient.h"

namespace {
constexpr int kMonitorKillMs = 2000;
}

RunSession::RunSession(QObject* parent)
    : QObject(parent),
      run_{"gps-run", "run.conf", "Running…", "Run finished: ", 0, {}, [this](const QString& status) { emit runFinished(status); }},
      monitor_{"gps-monitor", "monitor.conf", {}, "Monitor finished: ", kMonitorKillMs,
               [this](const QString& line) { sample(line); }, {}} {}

bool RunSession::run(const gps::Graph& plan) { return start(run_, plan); }

bool RunSession::startMonitoring(const gps::Graph& plan) { return start(monitor_, plan); }

void RunSession::stop() { run_.stop(); }

void RunSession::stopMonitoring() { monitor_.stop(); }

void RunSession::Channel::stop() const {
    if (process) process->interrupt(shutdownGraceMs);
}

bool RunSession::start(Channel& channel, const gps::Graph& plan) {
    gps::Graph copy = plan;
    copy.complete = true;
    const QString file = tmp_.filePath(channel.snapshot);
    std::string error;
    if (!copy.save(file.toStdString(), &error)) {
        emit notice("Unable to write the temporary plan file: " + QString::fromStdString(error));
        return false;
    }
    if (!channel.startMessage.isEmpty()) emit logLine(channel.startMessage);
    auto log = [this](const QString& line) { emit logLine(line); };
    auto done = [this, &channel](const HelperResult& result) { finished(channel, result); };
    channel.process = HelperClient::run(this, channel.helper, {file}, 0, log, done, channel.onLine);
    emit changed();
    return true;
}

void RunSession::sample(const QString& line) {
    gps::TelemetrySample sample;
    if (gps::fromJson(line.toStdString(), &sample)) emit telemetryReceived(sample);
}

void RunSession::finished(Channel& channel, const HelperResult& r) {
    channel.process = nullptr;
    gps::RunResult answer;
    answer.status.clear();
    const bool answered = r.read(&answer) && !answer.status.empty();
    QString status;
    if (r.outcome == HelperResult::Finished && answered) status = QString::fromStdString(answer.status);
    else if (r.outcome == HelperResult::Crashed) status = "crashed";
    else if (r.outcome == HelperResult::NotFound) status = "not-found";
    else status = "error";
    const QString message = answered ? QString::fromStdString(answer.message) : QString();
    if (r.outcome == HelperResult::NotFound) emit notice(channel.helper + " was not found");
    emit logLine(channel.finishPrefix + status + (message.isEmpty() ? "" : " – " + message));
    emit changed();
    if (channel.onFinished) channel.onFinished(status);
}
