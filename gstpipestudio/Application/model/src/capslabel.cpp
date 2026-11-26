// capslabel.cpp – see capslabel.h.
#include "capslabel.h"

#include <QRegularExpression>

namespace capslabel {

QStringList fields(const QString& caps) {
    QString first = caps.split(';').first().trimmed();
    if (first.isEmpty() || first == "ANY" || first == "EMPTY") return {};
    QStringList parts = first.split(',');
    QStringList out;
    out << "mediatype=" + parts[0].trimmed();
    static const QRegularExpression typeCast("\\([^)]+\\)");
    for (int i = 1; i < parts.size(); ++i) {
        QString field = parts[i].trimmed();
        field.remove(typeCast);
        if (!field.isEmpty()) out << field;
    }
    return out;
}

QString singleLine(const QString& caps) {
    const QString full = fields(caps).join(" · ");
    return full.size() > 60 ? full.left(57) + "…" : full;
}

QString multiLine(const QString& caps) {
    QStringList list = fields(caps);
    if (list.size() > 5) {
        list = list.mid(0, 5);
        list << "…";
    }
    return list.join("\n");
}

QFont font(QFont base) {
    base.setPointSizeF(7.5);
    return base;
}

}  // namespace capslabel
