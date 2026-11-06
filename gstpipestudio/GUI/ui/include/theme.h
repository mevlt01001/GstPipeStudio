// theme.h – the colours of UI_RULES 1 (rule 10) and the one style sheet built from them.
#pragma once

#include <QColor>
#include <QString>

class QApplication;

class Theme {
public:
    static QColor background() { return QColor("#202224"); }
    static QColor panel() { return QColor("#242424"); }
    static QColor box() { return QColor("#2a2a2a"); }
    static QColor line() { return QColor("#3a3a3a"); }
    static QColor text() { return QColor("#e8e8e8"); }
    static QColor muted() { return QColor("#9a9a9a"); }
    static QColor accent() { return QColor("#76B900"); }
    static QColor accentHover() { return QColor("#8ed000"); }
    static QColor onAccent() { return QColor("#101010"); }  // text on an accent button
    static QColor focusRing() { return accent(); }
    static QColor error() { return QColor("#ff6369"); }
    static QColor warning() { return QColor("#f5a524"); }

    // WCAG contrast ratio of two colours, 1 to 21.
    static double contrast(const QColor& a, const QColor& b);
    static QString styleSheet();
    // DejaVu Sans 10 and the style sheet.
    static void apply(QApplication& app);
};
