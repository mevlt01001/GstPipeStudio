// testsupport.h – small helpers shared by the GUI tests. They wait for the asynchronous helpers.
#pragma once

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "appservices.h"
#include "controller.h"
#include "settings.h"
#include "theme.h"

namespace support {

// Screenshots go to $GPS_SHOT_DIR when it is set, else to the folder CMake compiled in.
inline QString shotDir() { return qEnvironmentVariable("GPS_SHOT_DIR", GPS_SHOT_DIR); }

// Window size and splitter state go to a temporary place, never to the user's real settings.
inline void isolateSettings() {
    static QTemporaryDir dir;
    QCoreApplication::setOrganizationName("GstPipeStudioTests");
    QCoreApplication::setApplicationName("gui-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
}

inline bool waitFor(const std::function<bool()>& condition, int ms = 10000) { return QTest::qWaitFor(condition, ms); }

// The services of the test process, as main() has one: the catalog is loaded once, from the gps-inspect GPS_BIN_DIR
// names when it is first asked for.
inline AppServices& services() {
    static AppServices* shared = [] {
        auto* made = new AppServices(qApp);
        made->catalog().load();
        waitFor([made] { return made->catalog().loaded(); }, 30000);
        return made;
    }();
    return *shared;
}

// A Controller over the shared services. The library is read again: the test may have changed GPS_USER_DIR.
inline std::unique_ptr<Controller> makeController() {
    auto c = std::make_unique<Controller>(services());
    c->setUpdateTimeoutMs(5000);
    services().library().reload();
    return c;
}

inline bool settle(Controller& c) { return waitFor([&] { return !c.busy(); }); }

inline const ElementInfo* need(Controller& c, const QString& factory) {
    const ElementInfo* found = nullptr;
    bool done = false;
    c.services().catalog().fetchInfo(factory, &c, [&](const ElementInfo* i) {
        found = i;
        done = true;
    });
    waitFor([&] { return done; });
    return found;
}

// Each helper below returns the name of the new element, or "" when the edit was refused.
inline QString addSource(Controller& c, const QString& factory, const ElementEdit& edit = {}) {
    need(c, factory);
    if (!c.addElement(factory, edit)) return QString();
    settle(c);
    return QString::fromStdString(c.plan().elements.back().name);
}

inline QString addAfter(Controller& c, const QString& element, const QString& pad, const QString& factory,
                        const QString& sinkPad = "sink", const ElementEdit& edit = {}) {
    need(c, factory);
    if (!c.addAfter(element, pad, factory, sinkPad, edit)) return QString();
    settle(c);
    return QString::fromStdString(c.plan().elements.back().name);
}

inline ElementEdit withProps(const gps::Props& props) {
    ElementEdit e;
    e.props = props;
    return e;
}

}  // namespace support
