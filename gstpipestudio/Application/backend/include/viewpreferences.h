// viewpreferences.h – how every canvas of the process looks and moves: the link style, automatic alignment (both
// remembered) and moving connected elements together (this session only). The View menu sets them once; every
// canvas reads them here.
#pragma once

#include <QObject>

#include "layoutgraph.h"

class ViewPreferences : public QObject {
    Q_OBJECT
public:
    explicit ViewPreferences(QObject* parent = nullptr);

    layout::Routing routing() const { return routing_; }  // the next nice alignment routes links this way
    void setRouting(layout::Routing routing);
    bool autoAlign() const { return autoAlign_; }  // every edit is followed by a nice alignment of the whole canvas
    void setAutoAlign(bool on);
    bool connectedMove() const { return connectedMove_; }  // dragging an element moves its connected component
    void setConnectedMove(bool on);

signals:
    void changed();

private:
    layout::Routing routing_;
    bool autoAlign_;
    bool connectedMove_ = false;
};
