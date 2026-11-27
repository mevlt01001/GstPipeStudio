// helperclient.cpp – QProcess wrapper with a watchdog; emits `finished` exactly once.
#include "helperclient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <csignal>

QString findHelper(const QString& name) {
    // GPS_BIN_DIR, when set, is the only place to look: an override that silently falls back to another copy of
    // the helpers (next to the executable in a full build) would hide a wrong path.
    const QString override = qEnvironmentVariable("GPS_BIN_DIR");
    const QString directory = override.isEmpty() ? QCoreApplication::applicationDirPath() : override;
    const QFileInfo file(directory + "/" + name);
    return file.isFile() && file.isExecutable() ? file.absoluteFilePath() : QString();
}

QString findSystemTool(const QString& name) {
    return qEnvironmentVariableIsSet("GPS_BIN_DIR") ? findHelper(name) : QStandardPaths::findExecutable(name);
}

HelperClient* HelperClient::run(QObject* parent, const QString& name, const QStringList& args, int killAfterMs,
                                std::function<void(const QString&)> log, std::function<void(const HelperResult&)> done,
                                std::function<void(const QString&)> line) {
    auto* client = new HelperClient(parent);
    connect(client, &HelperClient::stderrLine, parent, [log, name](const QString& text) { log("[" + name + "] " + text); });
    if (line) {
        client->streaming_ = true;
        connect(client->process_, &QProcess::readyReadStandardOutput, client, &HelperClient::readOutput);
        connect(client, &HelperClient::stdoutLine, parent, line);
    }
    connect(client, &HelperClient::finished, parent, [client, done](const HelperResult& r) {
        client->deleteLater();
        done(r);
    });
    client->start(findHelper(name), args, killAfterMs);
    return client;
}

HelperClient::HelperClient(QObject* parent)
    : QObject(parent), process_(new QProcess(this)), watchdog_(new QTimer(this)), escalate_(new QTimer(this)) {
    escalate_->setSingleShot(true);
    escalate_->setTimerType(Qt::PreciseTimer);
    connect(escalate_, &QTimer::timeout, this, [this] { process_->kill(); });
    watchdog_->setSingleShot(true);
    watchdog_->setTimerType(Qt::PreciseTimer);  // a coarse timer may fire up to 5 % early: "timeout + 2 s" is a promise
    connect(process_, &QProcess::readyReadStandardError, this, &HelperClient::readError);
    connect(process_, &QProcess::finished, this, &HelperClient::onFinished);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(HelperResult::NotFound, 0);
        else emit stderrLine("helper process error: " + process_->errorString());
    });
    connect(watchdog_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_->kill();
    });
}

void HelperClient::start(const QString& program, const QStringList& args, int killAfterMs) {
    if (program.isEmpty()) {
        QTimer::singleShot(0, this, [this] { finish(HelperResult::NotFound, 0); });
        return;
    }
    process_->start(program, args);
    if (killAfterMs > 0) watchdog_->start(killAfterMs);
}

void HelperClient::interrupt(int killAfterMs) {
    if (process_->state() != QProcess::Running) return;
    ::kill(static_cast<pid_t>(process_->processId()), SIGINT);
    if (killAfterMs > 0) escalate_->start(killAfterMs);
}

void HelperClient::cancel() {
    if (done_ || process_->state() == QProcess::NotRunning) return;
    cancelled_ = true;
    process_->terminate();
    escalate_->start(kKillAfterTermMs);
}

void HelperClient::readError() {
    errBuffer_ += process_->readAllStandardError();
    int end;
    while ((end = errBuffer_.indexOf('\n')) >= 0) {
        emit stderrLine(QString::fromUtf8(errBuffer_.left(end)));
        errBuffer_.remove(0, end + 1);
    }
}

void HelperClient::readOutput() {
    outBuffer_ += process_->readAllStandardOutput();
    int end;
    while ((end = outBuffer_.indexOf('\n')) >= 0) {
        lastLine_ = outBuffer_.left(end);
        outBuffer_.remove(0, end + 1);
        emit stdoutLine(QString::fromUtf8(lastLine_));
    }
}

void HelperClient::onFinished(int code, QProcess::ExitStatus status) {
    readError();
    if (streaming_) readOutput();
    if (!errBuffer_.isEmpty()) emit stderrLine(QString::fromUtf8(errBuffer_));
    errBuffer_.clear();
    HelperResult::Outcome outcome = cancelled_ ? HelperResult::Cancelled
                                    : timedOut_ ? HelperResult::TimedOut
                                    : status == QProcess::CrashExit ? HelperResult::Crashed
                                                                    : HelperResult::Finished;
    finish(outcome, code);
}

void HelperClient::finish(HelperResult::Outcome outcome, int code) {
    if (done_) return;
    done_ = true;
    watchdog_->stop();
    escalate_->stop();

    HelperResult r;
    r.outcome = outcome;
    r.exitCode = code;
    if (streaming_) r.out = outBuffer_.trimmed().isEmpty() ? lastLine_ : outBuffer_;
    else if (process_->isOpen()) r.out = process_->readAllStandardOutput();
    emit finished(r);
}
