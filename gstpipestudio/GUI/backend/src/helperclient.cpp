// helperclient.cpp – QProcess wrapper with a watchdog; emits `finished` exactly once.
#include "helperclient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QTimer>
#include <csignal>

QString findHelper(const QString& name) {
    // GPS_BIN_DIR, when set, is the only place to look: an override that silently falls back to another copy of
    // the helpers (next to the executable in a full build) would hide a wrong path.
    QStringList dirs;
    QString env = qEnvironmentVariable("GPS_BIN_DIR");
    dirs << (env.isEmpty() ? QCoreApplication::applicationDirPath() : env);
    for (const QString& dir : dirs) {
        QFileInfo file(dir + "/" + name);
        if (file.isFile() && file.isExecutable()) return file.absoluteFilePath();
    }
    return QString();
}

QString findSystemTool(const QString& name) {
    return qEnvironmentVariableIsSet("GPS_BIN_DIR") ? findHelper(name) : QStandardPaths::findExecutable(name);
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

void HelperClient::interrupt() {
    if (process_->state() == QProcess::Running) ::kill(static_cast<pid_t>(process_->processId()), SIGINT);
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

void HelperClient::onFinished(int code, QProcess::ExitStatus status) {
    readError();
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
    if (process_->isOpen()) r.out = process_->readAllStandardOutput();
    QJsonDocument doc = QJsonDocument::fromJson(r.out);
    r.hasJson = doc.isObject();
    if (r.hasJson) r.json = doc.object();
    emit finished(r);
}
