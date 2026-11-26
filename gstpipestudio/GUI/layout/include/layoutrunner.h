// layoutrunner.h – runs a layout::Engine on its own thread, so the window never waits for a layout. Requests carry a
// ticket; while one runs, a newer request replaces the waiting one, and only the answer to the newest ticket is
// delivered (on the GUI thread).
#pragma once

#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

#include "layoutgraph.h"

namespace layout {

class Runner : public QObject {
    Q_OBJECT
public:
    using Factory = std::function<std::unique_ptr<Engine>()>;
    explicit Runner(Factory factory, QObject* parent = nullptr);
    ~Runner() override;

    // The answer arrives as finished(ticket, ok, result, error); an older ticket's answer never does.
    quint64 request(const Node& root, Routing routing);
    quint64 latest() const { return ticket_.load(); }

signals:
    void finished(quint64 ticket, bool ok, const layout::Result& result, const QString& error);
    void requested(quint64 ticket, const layout::Node& root, layout::Routing routing);  // to the worker

private:
    QThread thread_;
    std::atomic<quint64> ticket_{0};
};

}  // namespace layout

Q_DECLARE_METATYPE(layout::Node)
Q_DECLARE_METATYPE(layout::Result)
Q_DECLARE_METATYPE(layout::Routing)
