// templatepeek.cpp – see templatepeek.h.
#include "templatepeek.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QToolTip>

#include "contract.h"
#include "elkengine.h"
#include "layoutrunner.h"
#include "templates.h"
#include "theme.h"

namespace {

constexpr double kBoxW = 150, kHead = 26, kRow = 18, kMargin = 14, kTitle = 26, kParamLine = 16, kFramePad = 10;
constexpr double kMaxW = 620, kMaxH = 420;  // the picture is scaled down to fit

QString port(const std::string& element, const std::string& pad, bool src) {
    return fromStd(element + "|" + pad + (src ? "|src" : "|sink"));
}

// Its own engine on its own thread: a peek never waits for, nor delays, a canvas layout.
layout::Runner& runner() {
    static auto* shared = new layout::Runner([] { return std::make_unique<layout::ElkEngine>(); }, qApp);
    return *shared;
}

}  // namespace

QPointer<TemplatePeek> TemplatePeek::shown_;

layout::Node TemplatePeek::tree(const gps::Graph& content) {
    layout::Node root;
    root.id = "root";
    QHash<QString, layout::Node> boxes;
    QHash<QString, int> srcRows, sinkRows;
    for (const gps::Element& e : content.elements) {
        layout::Node box;
        box.id = fromStd(e.name);
        box.width = kBoxW;
        boxes[box.id] = box;
    }
    for (size_t i = 0; i < content.links.size(); ++i) {
        const gps::Connection& l = content.links[i];
        const QString from = fromStd(l.srcElement), to = fromStd(l.sinkElement);
        if (!boxes.contains(from) || !boxes.contains(to)) continue;
        const QString a = port(l.srcElement, l.srcPad, true), b = port(l.sinkElement, content.sinkIdentity(i), false);
        boxes[from].ports.append({a, true, kHead + (srcRows[from]++ + 0.5) * kRow});
        boxes[to].ports.append({b, false, kHead + (sinkRows[to]++ + 0.5) * kRow});
        root.edges.append({QString::number(i), a, b});
    }
    for (const gps::Element& e : content.elements) {
        layout::Node box = boxes.value(fromStd(e.name));
        box.height = kHead + std::max(1, std::max(srcRows.value(box.id), sinkRows.value(box.id))) * kRow + 6;
        root.children.append(box);
    }
    return root;
}

TemplatePeek::TemplatePeek(const gps::Graph& content, QWidget* parent) : QFrame(parent, Qt::ToolTip), content_(content) {
    setObjectName("templatePeek");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    QToolTip::hideText();
    qApp->installEventFilter(this);
    for (const gps::ExposedParam& p : content.exposed) params_ << fromStd(p.name.empty() ? gps::templates::paramName(content, p) : p.name);
    const quint64 ticket = runner().request(tree(content), layout::Routing::Curves);
    connect(&runner(), &layout::Runner::finished, this, [this, ticket](quint64 answered, bool ok, const layout::Result& result) {
        if (answered == ticket && ok) place(result);
    });
}

QSize TemplatePeek::sizeHint() const { return canvas_.toSize(); }

void TemplatePeek::place(const layout::Result& result) {
    laid_ = result;
    QRectF all;
    for (const QRectF& r : result.nodes) all = all.isNull() ? r : all.united(r);
    scale_ = std::min({1.0, kMaxW / std::max(1.0, all.width()), kMaxH / std::max(1.0, all.height())});
    const double paramsHeight = params_.isEmpty() ? 0 : 20 + params_.size() * kParamLine;
    canvas_ = QSizeF(std::max(all.width() * scale_ + 2 * kMargin, 240.0), kTitle + all.height() * scale_ + 2 * kMargin + paramsHeight);
    resize(sizeHint());
    QPoint at = at_;
    if (QScreen* screen = QGuiApplication::screenAt(at_)) {
        const QRect room = screen->availableGeometry();
        at.setX(std::min(at.x(), room.right() - width()));
        at.setY(std::min(at.y(), room.bottom() - height()));
    }
    move(at);
    laidOut_ = true;
    QFrame::show();
}

void TemplatePeek::show(const gps::Graph& content, const QPoint& globalPos, QWidget* parent) {
    hide();
    auto* peek = new TemplatePeek(content, parent);
    peek->at_ = globalPos + QPoint(18, 18);
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
    if (!laidOut_) return;

    // The small canvas: ELK's coordinates, scaled to fit under the title.
    QRectF all;
    for (const QRectF& r : laid_.nodes) all = all.isNull() ? r : all.united(r);
    p.save();
    p.translate(kMargin, kTitle + kMargin);
    p.scale(scale_, scale_);
    p.translate(-all.topLeft());

    // A nested template: a green frame around its members.
    QFont small = font();
    small.setPointSizeF(8.5);
    for (const gps::TemplateInstance& t : content_.instances) {
        QRectF around;
        for (const std::string& m : gps::templates::members(content_, t.id)) around = around.isNull() ? laid_.nodes.value(fromStd(m)) : around.united(laid_.nodes.value(fromStd(m)));
        if (around.isNull()) continue;
        p.setPen(QPen(Theme::accent(), 1.2 / scale_));
        p.setBrush(QColor(118, 185, 0, 14));
        p.drawRoundedRect(around.adjusted(-kFramePad, -kFramePad - 14, kFramePad, kFramePad), 8, 8);
        p.setFont(small);
        p.drawText(around.adjusted(-kFramePad + 6, -kFramePad - 14, 0, 0).topLeft() + QPointF(0, 11), fromStd(t.templateName));
    }
    // Links along their routes.
    p.setPen(QPen(QColor("#5f9400"), 1.6 / scale_));
    p.setBrush(Qt::NoBrush);
    for (const auto& pieces : laid_.edges) {
        for (const QList<QPointF>& piece : pieces) {
            if (piece.size() < 2) continue;
            QPainterPath line(piece.front());
            for (int i = 1; i < piece.size(); ++i) line.lineTo(piece[i]);
            p.drawPath(line);
        }
    }
    // Boxes with the name the user reads, and their linked pads.
    const auto names = content_.displayNames();
    for (const gps::Element& e : content_.elements) {
        const QRectF r = laid_.nodes.value(fromStd(e.name));
        if (r.isNull()) continue;
        p.setPen(QPen(Theme::line(), 1 / scale_));
        p.setBrush(Theme::box());
        p.drawRoundedRect(r, 6, 6);
        p.setFont(small);
        p.setPen(Theme::text());
        p.drawText(r.adjusted(8, 4, -8, -(r.height() - kHead)), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(small).elidedText(fromStd(names.at(e.name)), Qt::ElideRight, int(r.width() - 16)));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::accent());
    for (const QPointF& at : laid_.ports) p.drawEllipse(at, 3.5, 3.5);
    p.restore();

    if (!params_.isEmpty()) {
        double y = height() - kMargin - params_.size() * kParamLine;
        p.setPen(Theme::accent());
        p.drawText(QRectF(kMargin, y - 18, width() - 2 * kMargin, 16), Qt::AlignLeft | Qt::AlignVCenter, "Parameters");
        p.setPen(Theme::text());
        for (const QString& name : params_) {
            p.drawText(QRectF(kMargin + 8, y, width() - 2 * kMargin, kParamLine), Qt::AlignLeft | Qt::AlignVCenter, name);
            y += kParamLine;
        }
    }
}
