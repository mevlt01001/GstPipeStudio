// runsession.h – the pipeline of one document running: by gps-run, or with live metrics by gps-monitor. Both start the
// same way, on a temporary copy of the canvas as it is (the file is written only by Save), and end the same way: the
// helper's answer says how it finished.
#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <functional>

#include "wire.h"

namespace gps {
struct Graph;
}
class HelperClient;
struct HelperResult;

class RunSession : public QObject {
    Q_OBJECT
public:
    explicit RunSession(QObject* parent = nullptr);

    bool run(const gps::Graph& plan);  // false when the copy cannot be written
    void stop();                       // Ctrl+C, as a terminal stops gst-launch
    bool startMonitoring(const gps::Graph& plan);
    void stopMonitoring();             // Ctrl+C, killed 2 s later when it does not stop
    bool running() const { return run_.process != nullptr; }
    bool monitoring() const { return monitor_.process != nullptr; }

signals:
    void changed();
    void runFinished(const QString& status);
    void telemetryReceived(const gps::TelemetrySample& sample);
    void logLine(const QString& line);
    void notice(const QString& text);

private:
    struct Channel {
        QString helper;
        QString snapshot;
        QString startMessage;
        QString finishPrefix;
        int shutdownGraceMs;
        std::function<void(const QString&)> onLine;
        std::function<void(const QString&)> onFinished;
        HelperClient* process = nullptr;

        void stop() const;
    };

    bool start(Channel& channel, const gps::Graph& plan);
    void finished(Channel& channel, const HelperResult& result);
    void sample(const QString& line);

    QTemporaryDir tmp_;
    Channel run_;
    Channel monitor_;
};
