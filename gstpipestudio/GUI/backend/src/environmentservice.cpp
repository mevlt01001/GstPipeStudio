// environmentservice.cpp – see environmentservice.h.
#include "environmentservice.h"

#include "factorycatalog.h"

EnvironmentService::EnvironmentService(FactoryCatalog& catalog, QObject* parent) : QObject(parent), catalog_(catalog) {
    connect(&check_, &StartupCheck::finished, this, [this](const StartupResult& r) {
        if (r.state == StartupResult::Ready && !catalog_.loaded()) catalog_.load();
        emit checked(r);
    });
}

void EnvironmentService::check() { check_.start(); }
