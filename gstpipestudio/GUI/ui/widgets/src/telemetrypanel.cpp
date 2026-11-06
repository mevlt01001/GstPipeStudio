#include "telemetrypanel.h"
#include "controller.h"
#include "theme.h"
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
double value(const QJsonObject& object, const char* key) { return object.value(key).isDouble() ? object.value(key).toDouble() : -1; }
QString shown(double n, const QString& unit = {}) { return n < 0 ? "—" : QString::number(n, 'f', 1) + unit; }
class TrendChart final : public QWidget {
public:
    explicit TrendChart(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(140); }
    QVector<double> points;
    QString unit;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = rect().adjusted(55, 14, -12, -27);
        p.setPen(Theme::muted());
        p.drawRect(area);
        double maximum = 0;
        for (double v : points) if (v >= 0 && std::isfinite(v)) maximum = std::max(maximum, v);
        if (maximum <= 0) maximum = 1;
        p.drawText(2, 22, QString::number(maximum, 'f', 1) + unit);
        p.drawText(2, height() - 28, "0");
        p.drawText(area.bottomLeft() + QPointF(0, 19), "older");
        p.drawText(area.bottomRight() + QPointF(-28, 19), "now");
        p.setPen(QPen(Theme::accent(), 2));
        QPainterPath path;
        bool open = false, any = false;
        for (int i = 0; i < points.size(); ++i) {
            double v = points[i];
            if (v < 0 || !std::isfinite(v)) { open = false; continue; }
            const double x = area.left() + (points.size() < 2 ? 1.0 : static_cast<double>(i) / (points.size()-1)) * area.width();
            const double y = area.bottom() - std::min(v / maximum, 1.0) * area.height();
            if (!open) path.moveTo(x, y); else path.lineTo(x, y);
            open = any = true;
        }
        p.drawPath(path);
        if (!any) { p.setPen(Theme::muted()); p.drawText(area, Qt::AlignCenter, "No valid samples for this metric"); }
    }
};
} // namespace

TelemetryPanel::TelemetryPanel(Controller* controller, QWidget* parent) : QWidget(parent), controller_(controller) {
    setObjectName("telemetryPanel");
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    toggle_ = new QPushButton("Start monitoring", this); toggle_->setObjectName("monitorToggle");
    element_ = new QComboBox(this); element_->setObjectName("monitorElement");
    metric_ = new QComboBox(this); metric_->setObjectName("monitorMetric");
    metric_->addItems({"Source age (ms)", "Queue level (ms)", "QoS loss (%)", "Input buffers/s", "Output buffers/s"});
    controls->addWidget(toggle_); controls->addWidget(new QLabel("Element:", this)); controls->addWidget(element_);
    controls->addWidget(new QLabel("Graph:", this)); controls->addWidget(metric_); controls->addStretch();
    layout->addLayout(controls);
    auto* note = new QLabel("Source age requires a live source with valid PTS. QoS loss appears only when an element reports it. Queue level is buffered media duration; adding a queue can increase latency.", this);
    note->setWordWrap(true); layout->addWidget(note);
    chart_ = new TrendChart(this); chart_->setObjectName("monitorChart"); layout->addWidget(chart_);
    table_ = new QTableWidget(0, 6, this); table_->setObjectName("monitorTable");
    table_->setHorizontalHeaderLabels({"Element", "Age ms", "Queue ms", "QoS loss %", "Input/s", "Output/s"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide(); table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);
    connect(toggle_, &QPushButton::clicked, this, [this] {
        if (controller_->monitoring()) controller_->stopMonitoring();
        else { history_.clear(); element_->clear(); table_->setRowCount(0); lastElapsed_ = -1; controller_->startMonitoring(); }
        redraw();
    });
    connect(controller_, &Controller::changed, this, &TelemetryPanel::redraw);
    connect(controller_, &Controller::telemetryReceived, this, &TelemetryPanel::receive);
    connect(element_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TelemetryPanel::redraw);
    connect(metric_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TelemetryPanel::redraw);
    redraw();
}
void TelemetryPanel::receive(const QJsonObject& sample) {
    const bool first = history_.isEmpty();
    QString preferred;
    const double elapsed = value(sample, "elapsedMs");
    const double seconds = lastElapsed_ >= 0 && elapsed > lastElapsed_ ? (elapsed - lastElapsed_) / 1000.0 : -1;
    lastElapsed_ = elapsed;
    const QJsonArray elements = sample.value("elements").toArray();
    table_->setRowCount(elements.size());
    for (int row = 0; row < elements.size(); ++row) {
        const QJsonObject object = elements[row].toObject();
        const QString name = object.value("element").toString();
        if (name.isEmpty()) continue;
        if (element_->findText(name) < 0) element_->addItem(name);
        TrendPoint point;
        point.age = value(object, "ageMs"); point.queue = value(object, "queueMs"); point.loss = value(object, "lossPercent");
        if (first && preferred.isEmpty() && point.age >= 0) preferred = name;
        if (seconds > 0) {
            point.inputRate = value(object, "inputBuffers") / seconds;
            point.outputRate = value(object, "outputBuffers") / seconds;
        }
        auto& series = history_[name]; series.push_back(point);
        if (series.size() > 120) series.remove(0, series.size() - 120);
        const QStringList cells = {name, shown(point.age), shown(point.queue), shown(point.loss), shown(point.inputRate), shown(point.outputRate)};
        for (int col = 0; col < cells.size(); ++col) table_->setItem(row, col, new QTableWidgetItem(cells[col]));
    }
    if (first && !preferred.isEmpty()) element_->setCurrentText(preferred);
    redraw();
}
void TelemetryPanel::redraw() {
    toggle_->setText(controller_->monitoring() ? "Stop monitoring" : "Start monitoring");
    toggle_->setEnabled(controller_->monitoring() || controller_->canRun());
    auto* chart = static_cast<TrendChart*>(chart_);
    chart->points.clear();
    chart->unit = metric_->currentIndex() < 2 ? " ms" : metric_->currentIndex() == 2 ? " %" : "/s";
    for (const TrendPoint& p : history_.value(element_->currentText())) {
        switch (metric_->currentIndex()) {
        case 0: chart->points << p.age; break;
        case 1: chart->points << p.queue; break;
        case 2: chart->points << p.loss; break;
        case 3: chart->points << p.inputRate; break;
        case 4: chart->points << p.outputRate; break;
        }
    }
    chart->update();
}
