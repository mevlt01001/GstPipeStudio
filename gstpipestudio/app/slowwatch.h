// slowwatch.h – decides when a validation is "slow" (UI_RULES 3.2): the first question comes after 60 s,
// every Wait adds 15 s. The clock is injectable, so a test moves time instead of waiting for it.
#pragma once

#include <QObject>
#include <QTimer>
#include <functional>

class SlowWatch : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kFirstLimitMs = 60000;
    static constexpr qint64 kExtraMs = 15000;

    using Clock = std::function<qint64()>;  // milliseconds, any origin

    explicit SlowWatch(QObject* parent = nullptr);

    void setClock(Clock clock) { clock_ = std::move(clock); }
    void start();   // a validation began
    void stop();    // it ended
    void extend();  // the user chose Wait: the next question comes kExtraMs later
    void check();   // compares the clock with the limit; the timer calls it every 250 ms while running

    bool running() const { return running_; }
    bool asking() const { return asking_; }
    qint64 elapsedMs() const { return running_ ? clock_() - startedAt_ : 0; }

signals:
    void slow();    // once per question
    void ticked();  // elapsed time changed

private:
    Clock clock_;
    QTimer timer_;
    qint64 startedAt_ = 0;
    qint64 limitMs_ = kFirstLimitMs;
    bool running_ = false;
    bool asking_ = false;
};
