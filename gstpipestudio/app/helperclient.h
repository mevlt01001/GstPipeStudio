// helperclient.h – one asynchronous call of a helper program (gps-inspect, gps-update, gps-run).
// The helpers answer with one JSON object on stdout and log on stderr (SPEC 7). A watchdog kills a hung helper.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

class QTimer;

struct HelperResult {
    enum Outcome { Finished, Crashed, TimedOut, NotFound };

    Outcome outcome = Finished;
    int exitCode = 0;  // for Crashed: the signal number
    QByteArray out;
    bool hasJson = false;
    QJsonObject json;  // the stdout object, when it parsed
};

// GPS_BIN_DIR when it is set, else the folder of this executable. Empty when the helper is nowhere.
QString findHelper(const QString& name);

class HelperClient : public QObject {
    Q_OBJECT
public:
    explicit HelperClient(QObject* parent = nullptr);

    // killAfterMs <= 0: no watchdog. An empty program (helper not found) finishes with NotFound.
    void start(const QString& program, const QStringList& args, int killAfterMs);
    void interrupt();  // SIGINT, the way a terminal's Ctrl+C stops gst-launch

signals:
    void stderrLine(const QString& line);
    void finished(const HelperResult& result);

private:
    void readError();
    void onFinished(int code, QProcess::ExitStatus status);
    void finish(HelperResult::Outcome outcome, int code);

    QProcess* process_;
    QTimer* watchdog_;
    QByteArray errBuffer_;
    bool timedOut_ = false;
    bool done_ = false;
};
