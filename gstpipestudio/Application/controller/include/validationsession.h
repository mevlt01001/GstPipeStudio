// validationsession.h – the validation of one document by gps-update. An edit that changes what runs asks for one; a
// burst of edits is validated once, a short pause after the last (the edits go on meanwhile). A slow validation asks
// the user (SlowWatch: Wait or Cancel); a cancelled one leaves the plan "not validated" until Validate.
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "documenttiming.h"
#include "executionbackend.h"
#include "pipeline.h"

class SlowWatch;

class ValidationSession : public QObject {
    Q_OBJECT
public:
    ValidationSession(gps::Pipeline& pipeline, const DocumentTiming& timing, QObject* parent = nullptr);

    // A validation soon; suspect is the element the last edit concerned, named when the validator dies.
    void request(const QString& suspect = QString());
    void cancel();    // SIGTERM, SIGKILL after 2 s; the plan keeps its change and is "not validated"
    void waitMore();  // the user chose Wait: the next question comes 15 s later
    void forget() { cancelled_ = false; }  // a new plan: no cancelled validation to tell of
    bool updating() const { return updating_; }
    bool cancelling() const { return cancelling_; }
    bool cancelled() const { return cancelled_; }  // the user cancelled the last one
    SlowWatch* slowWatch() const { return watch_; }
    bool slowAsked() const;
    void setTimeoutMs(int ms) { backend_.setTimeout(ms); }

signals:
    void changed();
    void finished(bool ok);  // validated (ok: no problem), or cancelled (false)
    void logLine(const QString& line);

private:
    void dispatch();
    void finish(const gps::ExecutionResult& result);
    void stopped(bool cancelled);  // no longer running: cancelled, or done

    gps::Pipeline& pipeline_;
    HelperExecutionBackend backend_;
    gps::ExecutionCoordinator execution_{pipeline_, backend_};
    QTimer pause_;  // the short pause that gathers a burst of edits into one validation
    SlowWatch* watch_;
    QString suspect_;
    bool updating_ = false;
    bool cancelling_ = false;
    bool cancelled_ = false;
};
