// viewpreferences.cpp – see viewpreferences.h.
#include "viewpreferences.h"

#include "settings.h"

ViewPreferences::ViewPreferences(QObject* parent)
    : QObject(parent), routing_(Settings::linkRouting()), autoAlign_(Settings::autoAlign()) {}

void ViewPreferences::setRouting(layout::Routing routing) {
    if (routing == routing_) return;
    routing_ = routing;
    Settings::setLinkRouting(routing);
    emit changed();
}

void ViewPreferences::setAutoAlign(bool on) {
    if (on == autoAlign_) return;
    autoAlign_ = on;
    Settings::setAutoAlign(on);
    emit changed();
}

void ViewPreferences::setConnectedMove(bool on) {
    if (on == connectedMove_) return;
    connectedMove_ = on;
    emit changed();
}
