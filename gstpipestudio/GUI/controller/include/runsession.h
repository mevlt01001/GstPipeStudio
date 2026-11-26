// runsession.h – the pipeline of one document running: by gps-run, or with live metrics by gps-monitor. Both start the
// same way, on a temporary copy of the canvas as it is (the file is written only by Save), and end the same way: the
// helper's answer says how it finished.
#pragma once

#include <QObject>
#include <QTemporaryDir>

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
    bool running() const { return run_ != nullptr; }
    bool monitoring() const { return monitor_ != nullptr; }

signals:
    void changed();
    void runFinished(const QString& status);
    void telemetryReceived(const gps::TelemetrySample& sample);
    void logLine(const QString& line);
    void notice(const QString& text);

private:
    enum class Mode { Run, Monitor };
    bool start(Mode mode, const gps::Graph& plan);
    void finished(Mode mode, const HelperResult& result);
    void sample(const QString& line);  // a line of gps-monitor: a telemetry sample (its last line is its answer)

    QTemporaryDir tmp_;
    HelperClient* run_ = nullptr;
    HelperClient* monitor_ = nullptr;
};
