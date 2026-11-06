// factorydelegate.cpp
#include "factorydelegate.h"

#include <QPainter>

#include "theme.h"

namespace {

// Draws a pill ending at `right`; returns its left edge.
int drawPill(QPainter* p, int right, const QRect& row, const QString& text, bool filled) {
    QFont font = p->font();
    font.setPointSizeF(font.pointSizeF() - 1.5);
    font.setBold(true);
    p->save();
    p->setFont(font);
    int width = QFontMetrics(font).horizontalAdvance(text) + 14;
    QRect pill(right - width, row.center().y() - 9, width, 18);
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(filled ? Qt::NoPen : QPen(Theme::accent(), 1));
    p->setBrush(filled ? Theme::accent() : Qt::NoBrush);
    p->drawRoundedRect(pill, 9, 9);
    p->setPen(filled ? QColor("#101010") : Theme::accent());
    p->drawText(pill, Qt::AlignCenter, text);
    p->restore();
    return pill.left() - 6;
}

}  // namespace

QSize FactoryDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const {
    return QSize(100, index.data(HeaderRole).toBool() ? 30 : 28);
}

void FactoryDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    p->save();
    QRect row = option.rect;
    if (index.data(HeaderRole).toBool()) {
        QFont font = option.font;
        font.setBold(true);
        font.setPointSizeF(font.pointSizeF() - 1);
        p->setFont(font);
        p->setPen(Theme::muted());
        p->drawText(row.adjusted(10, 6, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, index.data().toString().toUpper());
        p->setPen(Theme::line());
        p->drawLine(row.left() + 6, row.bottom(), row.right() - 6, row.bottom());
        p->restore();
        return;
    }

    if (option.state & QStyle::State_Selected) {
        p->fillRect(row, QColor("#34420f"));
        p->fillRect(QRect(row.left(), row.top(), 3, row.height()), Theme::accent());
    } else if (option.state & QStyle::State_MouseOver) {
        p->fillRect(row, QColor("#2e2e2e"));
    }

    int right = row.right() - 10;
    if (index.data(NvidiaRole).toBool()) right = drawPill(p, right, row, "NVIDIA", true);
    QString hint = index.data(HintRole).toString();
    if (!hint.isEmpty()) right = drawPill(p, right, row, hint, false);

    QFont bold = option.font;
    bold.setBold(true);
    p->setFont(bold);
    p->setPen(Theme::text());
    QString name = index.data().toString();
    int nameWidth = QFontMetrics(bold).horizontalAdvance(name);
    QRect nameRect(row.left() + 12, row.top(), qMin(nameWidth + 2, right - row.left() - 12), row.height());
    p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(bold).elidedText(name, Qt::ElideRight, nameRect.width()));

    int left = nameRect.right() + 12;
    if (right - left > 30) {
        p->setFont(option.font);
        p->setPen(Theme::muted());
        QString longName = index.data(LongNameRole).toString();
        p->drawText(QRect(left, row.top(), right - left, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(option.font).elidedText(longName, Qt::ElideRight, right - left));
    }
    p->restore();
}
