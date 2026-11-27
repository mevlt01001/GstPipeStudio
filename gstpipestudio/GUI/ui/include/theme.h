// The active desktop colour scheme and its shared widget and canvas roles.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <functional>

class QApplication;
class QWidget;

class Theme : public QObject {
    Q_OBJECT
public:
    enum class Mode { Dark, Light };

    static Mode mode();
    static void apply(QApplication& app, Mode mode = Mode::Dark);
    static void setMode(QApplication& app, Mode mode);
    static void onChanged(QObject* context, std::function<void()> refresh);
    static void setStyle(QWidget* widget, const QString& tokens);

    static QColor background();
    static QColor panel();
    static QColor box();
    static QColor line();
    static QColor text();
    static QColor muted();
    static QColor accent();
    static QColor accentHover();
    static QColor onAccent();
    static QColor focusRing() { return accent(); }
    static QColor error();
    static QColor warning();
    static QColor grid();
    static QColor elementHeader();
    static QColor templateHeader();
    static QColor link();
    static QColor selection();
    static QColor itemHover();
    static QColor chartSeries(int series);

    static double contrast(const QColor& a, const QColor& b);
    static QString styleSheet();

signals:
    void changed();

private:
    static Theme& instance();
    Mode mode_ = Mode::Dark;
};
