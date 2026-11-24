#pragma once
#include <QWidget>
#include <QMap>
#include <QVector>

#include "runtime.h"

class Controller;
class QComboBox;
class QTableWidget;
class QPushButton;

struct TrendPoint { double processing = -1, pending = -1, drop = -1, age = -1, queue = -1, inputRate = -1, outputRate = -1; };

class TelemetryPanel : public QWidget {
    Q_OBJECT
public:
    explicit TelemetryPanel(Controller* controller, QWidget* parent = nullptr);
    void receive(const gps::TelemetrySample& sample);
private:
    void redraw();
    Controller* controller_;
    QComboBox* element_;
    QComboBox* metric_;
    QPushButton* toggle_;
    QTableWidget* table_;
    QWidget* chart_;
    QMap<QString, QVector<TrendPoint>> history_;
    double lastElapsed_ = -1;
};
