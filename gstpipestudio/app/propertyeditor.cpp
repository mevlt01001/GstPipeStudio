// propertyeditor.cpp
#include "propertyeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>
#include <climits>
#include <memory>

namespace {

bool isNumber(const QString& kind) {
    return kind == "int" || kind == "uint" || kind == "int64" || kind == "uint64" || kind == "float" || kind == "double";
}

// The nick an enum text stands for (the text may also be the name or the number).
QString enumNick(const PropertyInfo& info, const QString& text) {
    for (const EnumValue& v : info.enumValues) {
        if (v.nick == text || v.name == text || v.value == text) return v.nick;
    }
    return text;
}

QStringList flagNicks(const PropertyInfo& info, const QString& text) {
    QStringList nicks;
    for (const QString& token : text.split(QRegularExpression("[+|,]"), Qt::SkipEmptyParts)) {
        QString t = token.trimmed();
        bool numeric = false;
        qulonglong bits = t.toULongLong(&numeric);
        for (const EnumValue& v : info.enumValues) {
            qulonglong value = v.value.toULongLong();
            if (v.nick == t || v.name == t || (numeric && value != 0 && (bits & value) == value)) nicks << v.nick;
        }
    }
    return nicks;
}

// Compares two texts the way GStreamer would read them.
bool sameValue(const PropertyInfo& info, const QString& a, const QString& b) {
    if (isNumber(info.kind)) {
        bool okA = false, okB = false;
        double x = a.toDouble(&okA), y = b.toDouble(&okB);
        if (okA && okB) return x == y;
    }
    if (info.kind == "boolean") return a.toLower() == b.toLower();
    if (info.kind == "enum") return enumNick(info, a) == enumNick(info, b);
    if (info.kind == "flags") {
        QStringList x = flagNicks(info, a), y = flagNicks(info, b);
        x.sort();
        y.sort();
        return x == y;
    }
    return a == b;
}

}  // namespace

bool PropertyEditor::editable(const PropertyInfo& info) {
    static const QStringList kinds = {"boolean", "int", "uint", "int64", "uint64", "float", "double",
                                      "string",  "enum", "flags", "caps", "fraction"};
    return info.writable && kinds.contains(info.kind) && info.name != "name";
}

PropertyEditor::PropertyEditor(const PropertyInfo& info, const std::optional<QString>& current, QWidget* parent)
    : QWidget(parent), info_(info) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(build(current ? *current : info.defaultValue));
    setToolTip(info.blurb);
}

void PropertyEditor::touch() {
    touched_ = true;
    emit edited();
}

bool PropertyEditor::isDefault() const { return sameValue(info_, text(), info_.defaultValue); }

gps::Props PropertyEditor::collect(const QList<PropertyEditor*>& editors, const gps::Props& original) {
    gps::Props out = original;
    for (const PropertyEditor* e : editors) {
        if (!e->touched()) continue;
        if (e->isDefault()) out.erase(e->info().name.toStdString());
        else out[e->info().name.toStdString()] = e->text().toStdString();
    }
    return out;
}

QWidget* PropertyEditor::build(const QString& initial) {
    const QString& kind = info_.kind;

    if (kind == "boolean") {
        auto* box = new QCheckBox(this);
        box->setChecked(initial.toLower() == "true" || initial == "1");
        connect(box, &QCheckBox::toggled, this, &PropertyEditor::touch);
        read_ = [box] { return QString(box->isChecked() ? "true" : "false"); };
        return box;
    }

    if (kind == "int") {
        auto* spin = new QSpinBox(this);
        qlonglong lo = info_.min.isEmpty() ? INT_MIN : qMax<qlonglong>(info_.min.toLongLong(), INT_MIN);
        qlonglong hi = info_.max.isEmpty() ? INT_MAX : qMin<qlonglong>(info_.max.toLongLong(), INT_MAX);
        spin->setRange(static_cast<int>(lo), static_cast<int>(hi));
        spin->setValue(initial.toInt());
        spin->setMinimumWidth(130);
        connect(spin, &QSpinBox::valueChanged, this, &PropertyEditor::touch);
        read_ = [spin] { return QString::number(spin->value()); };
        return spin;
    }

    if (kind == "uint" || kind == "float" || kind == "double") {
        auto* spin = new QDoubleSpinBox(this);
        bool integral = kind == "uint";
        double lo = info_.min.isEmpty() ? (integral ? 0 : -1e9) : qMax(info_.min.toDouble(), integral ? 0.0 : -1e9);
        double hi = info_.max.isEmpty() ? (integral ? 4294967295.0 : 1e9) : qMin(info_.max.toDouble(), integral ? 4294967295.0 : 1e9);
        spin->setDecimals(integral ? 0 : 6);
        spin->setRange(lo, hi);
        spin->setSingleStep(integral ? 1 : 0.1);
        spin->setValue(initial.toDouble());
        spin->setMinimumWidth(130);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &PropertyEditor::touch);
        read_ = [spin] { return QString::number(spin->value(), 'g', 10); };
        return spin;
    }

    if (kind == "enum") {
        auto* combo = new QComboBox(this);
        for (const EnumValue& v : info_.enumValues) combo->addItem(v.nick + "  (" + v.value + ")", v.nick);
        combo->setCurrentIndex(qMax(0, combo->findData(enumNick(info_, initial))));
        connect(combo, &QComboBox::currentIndexChanged, this, &PropertyEditor::touch);
        read_ = [combo] { return combo->currentData().toString(); };
        return combo;
    }

    if (kind == "flags") {
        auto* box = new QWidget(this);
        auto* column = new QVBoxLayout(box);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(2);
        QStringList on = flagNicks(info_, initial);
        auto checks = std::make_shared<QList<QCheckBox*>>();
        for (const EnumValue& v : info_.enumValues) {
            auto* check = new QCheckBox(v.nick, box);
            check->setChecked(on.contains(v.nick));
            connect(check, &QCheckBox::toggled, this, &PropertyEditor::touch);
            column->addWidget(check);
            checks->append(check);
        }
        read_ = [checks] {
            QStringList nicks;
            for (const QCheckBox* c : *checks) {
                if (c->isChecked()) nicks << c->text();
            }
            return nicks.isEmpty() ? QString("0") : nicks.join("+");
        };
        return box;
    }

    // string, caps, fraction, int64, uint64: text
    auto* line = new QLineEdit(initial, this);
    if (kind == "int64") line->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]{1,19}"), line));
    if (kind == "uint64") line->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,20}"), line));
    if (kind == "fraction") line->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]+/[0-9]+"), line));
    if (kind == "int64" || kind == "uint64") line->setToolTip(QString("%1 … %2").arg(info_.min, info_.max));
    connect(line, &QLineEdit::textEdited, this, &PropertyEditor::touch);
    read_ = [line] { return line->text(); };
    return line;
}
