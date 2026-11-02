// factorydelegate.h – one row look for every list of elements: name, long name, and small pills (NVIDIA, uyumlu ...).
#pragma once

#include <QStyledItemDelegate>

enum FactoryRole {
    LongNameRole = Qt::UserRole + 1,  // muted text after the name
    NvidiaRole,                       // bool: green NVIDIA pill
    HintRole,                         // text of an outlined pill, e.g. "uyumlu"
    SearchRole,                       // lower-case text the search box matches
    HeaderRole,                       // bool: a section title, not selectable
};

class FactoryDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
