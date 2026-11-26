// helperclient.h – one asynchronous call of a helper program (gps-inspect, gps-update, gps-run, gps-monitor).
// The helpers answer with one JSON object on stdout and log on stderr (SPEC 7). A watchdog kills a hung helper.
// A streaming helper (gps-monitor) writes a JSON line per sample: each line is passed on as it comes, the last is the
// answer.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

#include "wire.h"

class QTimer;

struct HelperResult {
    enum Outcome { Finished, Crashed, TimedOut, NotFound, Cancelled };

    Outcome outcome = Finished;
    int exitCode = 0;  // for Crashed: the signal number
    QByteArray out;

    // The stdout answer as one of the wire.h structs; false when there is none or it is another kind of answer.
    template <class T> bool read(T* answer) const { return gps::fromJson(out.toStdString(), answer); }
};

// GPS_BIN_DIR when it is set, else the folder of this executable. Empty when the helper is nowhere.
QString findHelper(const QString& name);
// A system tool such as gst-inspect-1.0: looked up in PATH, or only in GPS_BIN_DIR when that is set (tests).
QString findSystemTool(const QString& name);

class HelperClient : public QObject {
    Q_OBJECT
public:
    static constexpr int kKillAfterTermMs = 2000;

    explicit HelperClient(QObject* parent = nullptr);

    // One call of helper `name` (found with findHelper): its stderr lines go to log as "[name] line", the client deletes
    // itself after done ran. The returned client can be interrupted or cancelled until then. With line, stdout is read
    // line by line as it comes and each line goes there; the result holds the last one.
    static HelperClient* run(QObject* parent, const QString& name, const QStringList& args, int killAfterMs,
                             std::function<void(const QString&)> log, std::function<void(const HelperResult&)> done,
                             std::function<void(const QString&)> line = {});

    // killAfterMs <= 0: no watchdog. An empty program (helper not found) finishes with NotFound.
    void start(const QString& program, const QStringList& args, int killAfterMs);
    void interrupt(int killAfterMs = 0);  // SIGINT, the way a terminal's Ctrl+C stops gst-launch; SIGKILL later when > 0
    void cancel();     // SIGTERM, then SIGKILL after kKillAfterTermMs; finishes with Cancelled

signals:
    void stderrLine(const QString& line);
    void stdoutLine(const QString& line);  // streaming only
    void finished(const HelperResult& result);

private:
    void readError();
    void readOutput();  // streaming: every whole line, the last kept as the answer
    void onFinished(int code, QProcess::ExitStatus status);
    void finish(HelperResult::Outcome outcome, int code);

    QProcess* process_;
    QTimer* watchdog_;
    QTimer* escalate_;  // SIGKILL for a helper that ignored SIGTERM
    QByteArray errBuffer_;
    QByteArray outBuffer_, lastLine_;
    bool streaming_ = false;
    bool timedOut_ = false;
    bool cancelled_ = false;
    bool done_ = false;
};
