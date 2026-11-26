// layoutrunner.cpp – see layoutrunner.h.
#include "layoutrunner.h"

#include <QPointer>

namespace layout {
namespace {

// Lives on the layout thread. Requests queue up as events; the ones a newer request made stale are skipped.
class Worker : public QObject {
public:
    Worker(Runner::Factory factory, const std::atomic<quint64>* newest) : factory_(std::move(factory)), newest_(newest) {}
    void run(Runner* owner, quint64 ticket, const Node& root, Routing routing) {
        if (ticket != newest_->load()) return;  // a newer request is waiting behind this one
        if (!engine_) engine_ = factory_();
        Result result;
        QString error;
        const bool ok = engine_->run(root, routing, &result, &error);
        QPointer<Runner> alive(owner);
        QMetaObject::invokeMethod(owner, [alive, ticket, ok, result, error] {
            if (alive && ticket == alive->latest()) emit alive->finished(ticket, ok, result, error);
        }, Qt::QueuedConnection);
    }

private:
    Runner::Factory factory_;
    std::unique_ptr<Engine> engine_;
    const std::atomic<quint64>* newest_;
};

}  // namespace

Runner::Runner(Factory factory, QObject* parent) : QObject(parent) {
    qRegisterMetaType<layout::Node>();
    qRegisterMetaType<layout::Result>();
    qRegisterMetaType<layout::Routing>();
    thread_.setObjectName("layout");
    thread_.setStackSize(16 * 1024 * 1024);  // the engine's JavaScript recurses deeply
    auto* worker = new Worker(std::move(factory), &ticket_);
    worker->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &Runner::requested, worker, [this, worker](quint64 ticket, const Node& root, Routing routing) {
        worker->run(this, ticket, root, routing);
    }, Qt::QueuedConnection);
    thread_.start();
}

Runner::~Runner() {
    ++ticket_;  // whatever still waits on the thread is stale now
    thread_.quit();
    thread_.wait();
}

quint64 Runner::request(const Node& root, Routing routing) {
    const quint64 ticket = ++ticket_;
    emit requested(ticket, root, routing);
    return ticket;
}

}  // namespace layout
