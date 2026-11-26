// componentprovider.cpp – see componentprovider.h. By default a provider only lists and places its components.
#include "componentprovider.h"

namespace {
bool refused(QString* error) {
    if (error) *error = "This kind of component cannot be changed here.";
    return false;
}
}  // namespace

QList<ComponentAction> ComponentProvider::actions(const QString&) const { return {}; }

QString ComponentProvider::nameError(const QString&, const QString&) const {
    QString error;
    refused(&error);
    return error;
}

bool ComponentProvider::rename(const QString&, const QString&, QString*, QString* error) { return refused(error); }

bool ComponentProvider::remove(const QString&) { return false; }

QString ComponentProvider::removalNote(const QString&) const { return QString(); }

bool ComponentProvider::exportTo(const QString&, const QString&, QString* error) const { return refused(error); }

QString ComponentProvider::importFrom(const QString&, QString* error) {
    refused(error);
    return QString();
}
