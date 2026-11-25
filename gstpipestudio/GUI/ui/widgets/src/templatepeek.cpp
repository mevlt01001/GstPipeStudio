// templatepeek.cpp – see templatepeek.h.
#include "templatepeek.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QToolTip>

#include "contract.h"
#include "templates.h"
#include "theme.h"

namespace {
constexpr double kBoxW = 132, kBoxH = 34, kColGap = 46, kRowGap = 14, kMargin = 14, kParamLine = 16;
}

QPointer<TemplatePeek> TemplatePeek::shown_;

TemplatePeek::TemplatePeek(const gps::Graph& content, QWidget* parent) : QFrame(parent, Qt::ToolTip), content_(content) {
    setObjectName("templatePeek");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    QToolTip::hideText();
    qApp->installEventFilter(this);
    // Columns by link depth: a quick, fixed picture (the canvas uses nice alignment).
    std::map<std::string, int> level = content.levels();
    QHash<int, int> rows;
    for (const gps::Element& e : content.elements) {
        const int column = level.count(e.name) ? level[e.name] : 0;
        const int row = rows[column]++;
        boxes_[fromStd(e.name)] = QRectF(kMargin + column * (kBoxW + kColGap), kMargin + 22 + row * (kBoxH + kRowGap), kBoxW, kBoxH);
    }
    QRectF all;
    for (const QRectF& r : boxes_) all = all.isNull() ? r : all.united(r);
    for (const gps::ExposedParam& p : content.exposed) params_ << fromStd(p.name.empty() ? gps::templates::paramName(content, p) : p.name);
    canvas_ = QSizeF(std::max(all.right() + kMargin, 220.0), all.bottom() + kMargin + (params_.isEmpty() ? 0 : 20 + params_.size() * kParamLine));
}

QSize TemplatePeek::sizeHint() const { return canvas_.toSize(); }

void TemplatePeek::show(const gps::Graph& content, const QPoint& globalPos, QWidget* parent) {
    hide();
    auto* peek = new TemplatePeek(content, parent);
    peek->resize(peek->sizeHint());
    QPoint at = globalPos + QPoint(18, 18);
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect room = screen->availableGeometry();
        at.setX(std::min(at.x(), room.right() - peek->width()));
        at.setY(std::min(at.y(), room.bottom() - peek->height()));
    }
    peek->move(at);
    peek->QFrame::show();
    shown_ = peek;
}

void TemplatePeek::hide() {
    if (shown_) shown_->deleteLater();
    shown_.clear();
}

TemplatePeek* TemplatePeek::current() { return shown_; }

bool TemplatePeek::eventFilter(QObject*, QEvent* event) { return event->type() == QEvent::ToolTip; }

void TemplatePeek::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::accent(), 1.5));
    p.setBrush(Theme::panel());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75), 10, 10);
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(Theme::accent());
    p.drawText(QRectF(kMargin, 6, width() - 2 * kMargin, 18), Qt::AlignLeft | Qt::AlignVCenter,
               fromStd(content_.name) + QString("  ·  %1 elements").arg(content_.elements.size()));

    p.setPen(QPen(QColor("#5f9400"), 1.4));
    for (const gps::Connection& l : content_.links) {
        const QRectF a = boxes_.value(fromStd(l.srcElement)), b = boxes_.value(fromStd(l.sinkElement));
        const QPointF from(a.right(), a.center().y()), to(b.left(), b.center().y());
        QPainterPath line(from);
        const double reach = std::max(20.0, (to.x() - from.x()) / 2);
        line.cubicTo(from + QPointF(reach, 0), to - QPointF(reach, 0), to);
        p.drawPath(line);
    }
    QFont small = font();
    small.setPointSizeF(8);
    const auto shown = content_.displayNames();
    for (const gps::Element& e : content_.elements) {
        const QRectF r = boxes_.value(fromStd(e.name));
        p.setPen(QPen(e.group.empty() ? Theme::line() : Theme::accentHover(), 1));  // a nested template's members are green-edged
        p.setBrush(Theme::box());
        p.drawRoundedRect(r, 5, 5);
        p.setFont(small);
        p.setPen(Theme::text());
        p.drawText(r.adjusted(6, 2, -6, -2), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(fromStd(shown.at(e.name)), Qt::ElideRight, int(r.width() - 12)));
    }
    if (!params_.isEmpty()) {
        double y = canvas_.height() - kMargin - params_.size() * kParamLine - 4;
        p.setPen(Theme::accent());
        p.drawText(QRectF(kMargin, y - 16, width() - 2 * kMargin, 16), Qt::AlignLeft | Qt::AlignVCenter, "Parameters");
        p.setPen(Theme::text());
        for (const QString& name : params_) {
            p.drawText(QRectF(kMargin + 8, y, width() - 2 * kMargin, kParamLine), Qt::AlignLeft | Qt::AlignVCenter, name);
            y += kParamLine;
        }
    }
}
