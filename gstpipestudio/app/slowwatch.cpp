// slowwatch.cpp
#include "slowwatch.h"

#include <QElapsedTimer>
#include <memory>

SlowWatch::SlowWatch(QObject* parent) : QObject(parent) {
    auto real = std::make_shared<QElapsedTimer>();
    real->start();
    clock_ = [real] { return real->elapsed(); };
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &SlowWatch::check);
}

void SlowWatch::start() {
    running_ = true;
    asking_ = false;
    limitMs_ = kFirstLimitMs;
    startedAt_ = clock_();
    timer_.start();
}

void SlowWatch::stop() {
    running_ = false;
    asking_ = false;
    timer_.stop();
}

void SlowWatch::extend() {
    if (!running_ || !asking_) return;
    asking_ = false;
    limitMs_ += kExtraMs;
    check();
}

void SlowWatch::check() {
    if (!running_) return;
    emit ticked();
    if (!asking_ && elapsedMs() >= limitMs_) {
        asking_ = true;
        emit slow();
    }
}
