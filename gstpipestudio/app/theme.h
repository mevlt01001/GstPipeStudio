// theme.h – the colours of SPEC 8 and the one style sheet built from them.
#pragma once

#include <QColor>
#include <QString>

class QApplication;

class Theme {
public:
    static QColor background() { return QColor("#1b1b1b"); }
    static QColor panel() { return QColor("#242424"); }
    static QColor box() { return QColor("#2a2a2a"); }
    static QColor line() { return QColor("#3a3a3a"); }
    static QColor text() { return QColor("#e8e8e8"); }
    static QColor muted() { return QColor("#9a9a9a"); }
    static QColor accent() { return QColor("#76B900"); }
    static QColor accentHover() { return QColor("#8ed000"); }
    static QColor error() { return QColor("#e5484d"); }
    static QColor warning() { return QColor("#f5a524"); }

    static QString styleSheet();
    // DejaVu Sans 10 and the style sheet.
    static void apply(QApplication& app);
};
