#pragma once
#include "helperclient.h"
#include "pipeline.h"
#include <QPointer>
#include <QTemporaryDir>

// Process isolation for native plugin crashes. Only immutable snapshots and
// value observations cross this boundary; no document pointer reaches a helper.
class HelperExecutionBackend final : public QObject, public gps::IExecutionBackend {
    Q_OBJECT
public:
    explicit HelperExecutionBackend(int timeoutMs, QObject* parent = nullptr) : QObject(parent), timeoutMs_(timeoutMs) {}
    void setTimeout(int ms) { timeoutMs_ = ms; }
    // The element the last edit touched: named as the cause when the helper crashes or hangs.
    void setSuspect(const std::string& element) { suspect_ = element; }
    void execute(const gps::ExecutionPlan&, Completion) override;
    void cancel() override;
signals:
    void logLine(const QString&);
private:
    struct Pending { gps::ExecutionPlan plan; Completion done; };
    std::optional<Pending> pending_;
    QTemporaryDir temporary_;
    QPointer<HelperClient> client_;
    int timeoutMs_;
    std::string suspect_;
};
