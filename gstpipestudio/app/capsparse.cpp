// capsparse.cpp
#include "capsparse.h"

#include <QMap>
#include <QStringList>

namespace {

// Splits at commas that are outside quotes and parentheses.
QStringList splitTopLevel(const QString& text) {
    QStringList parts;
    QString current;
    int depth = 0;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (quoted && c == '\\' && i + 1 < text.size()) {
            current += c;
            current += text[++i];
            continue;
        }
        if (c == '"') quoted = !quoted;
        if (!quoted && c == '(') ++depth;
        if (!quoted && c == ')') --depth;
        if (!quoted && depth == 0 && c == ',') {
            parts << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    parts << current.trimmed();
    return parts;
}

// The short type names CapsChoice::toText writes, back to GType names.
QString gtypeOf(const QString& shortName) {
    static const QMap<QString, QString> names = {
        {"string", "gchararray"}, {"int", "gint"},         {"uint", "guint"},      {"gint64", "gint64"},
        {"guint64", "guint64"},   {"float", "gfloat"},     {"double", "gdouble"},  {"boolean", "gboolean"},
        {"fraction", "GstFraction"}, {"bitmask", "GstBitmask"},
    };
    return names.value(shortName, shortName);
}

QString unquote(const QString& value) {
    if (value.size() < 2 || !value.startsWith('"') || !value.endsWith('"')) return value;
    QString out;
    for (int i = 1; i < value.size() - 1; ++i) {
        if (value[i] == '\\' && i + 1 < value.size() - 1) ++i;
        out += value[i];
    }
    return out;
}

}  // namespace

gps::CapsChoice parseCapsChoice(const QString& text) {
    gps::CapsChoice choice;
    QStringList parts = splitTopLevel(text);
    if (parts.isEmpty() || parts[0].isEmpty()) return choice;

    QString head = parts[0];
    int open = head.indexOf('(');
    choice.media = (open < 0 ? head : head.left(open)).trimmed().toStdString();
    if (open >= 0 && head.endsWith(')')) {
        for (const QString& f : head.mid(open + 1, head.size() - open - 2).split(',')) choice.features.push_back(f.trimmed().toStdString());
    }
    for (int i = 1; i < parts.size(); ++i) {
        int eq = parts[i].indexOf('=');
        if (eq < 0) continue;
        gps::CapsField field;
        field.name = parts[i].left(eq).trimmed().toStdString();
        QString rest = parts[i].mid(eq + 1).trimmed();
        QString type = "gchararray";
        if (rest.startsWith('(')) {
            int close = rest.indexOf(')');
            type = gtypeOf(rest.mid(1, close - 1));
            rest = rest.mid(close + 1);
        }
        field.type = type.toStdString();
        field.value = (type == "gchararray" ? unquote(rest) : rest).toStdString();
        choice.fields.push_back(field);
    }
    return choice;
}

