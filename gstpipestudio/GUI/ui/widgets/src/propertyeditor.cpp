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
#include <QToolButton>
#include <QVBoxLayout>
#include <climits>
#include <map>
#include <utility>

namespace {

// The nick an enum text stands for (the text may also be the name or the number).
QString enumNick(const PropertyInfo& info, const QString& text) {
    for (const EnumValue& v : info.enumValues) {
        if (fromStd(v.nick) == text || fromStd(v.name) == text || fromStd(v.value) == text) return fromStd(v.nick);
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
            qulonglong value = fromStd(v.value).toULongLong();
            if (fromStd(v.nick) == t || fromStd(v.name) == t || (numeric && value != 0 && (bits & value) == value)) nicks << fromStd(v.nick);
        }
    }
    return nicks;
}

bool sameText(const PropertyInfo&, const QString& left, const QString& right) { return left == right; }

bool sameNumber(const PropertyInfo&, const QString& left, const QString& right) {
    bool leftValid = false, rightValid = false;
    const double x = left.toDouble(&leftValid), y = right.toDouble(&rightValid);
    return leftValid && rightValid ? x == y : left == right;
}

bool sameBoolean(const PropertyInfo&, const QString& left, const QString& right) {
    return left.toLower() == right.toLower();
}

bool sameEnum(const PropertyInfo& info, const QString& left, const QString& right) {
    return enumNick(info, left) == enumNick(info, right);
}

bool sameFlags(const PropertyInfo& info, const QString& left, const QString& right) {
    QStringList x = flagNicks(info, left), y = flagNicks(info, right);
    x.sort();
    y.sort();
    return x == y;
}

}  // namespace

struct PropertyEditor::Style {
    std::function<QWidget*(PropertyEditor&, const QString&)> build;
    bool (*matchesDefault)(const PropertyInfo&, const QString&, const QString&) = sameText;

    static const Style& text() {
        static const Style style{[](PropertyEditor& editor, const QString& initial) { return editor.buildText(initial); }};
        return style;
    }
};

const PropertyEditor::Style* PropertyEditor::supportedStyle(const std::string& kind) {
    static const Style real{[](PropertyEditor& editor, const QString& initial) {
        return editor.buildNumber(initial, -1e9, 1e9, 6, 0.1);
    }, sameNumber};
    static const std::map<std::string, Style> styles{
        {"boolean", {&PropertyEditor::buildBoolean, sameBoolean}},
        {"int", {&PropertyEditor::buildInt, sameNumber}},
        {"uint", {[](PropertyEditor& editor, const QString& initial) {
            return editor.buildNumber(initial, 0, 4294967295.0, 0, 1);
        }, sameNumber}},
        {"int64", {[](PropertyEditor& editor, const QString& initial) {
            return editor.buildText(initial, "-?[0-9]{1,19}", editor.rangeHint());
        }, sameNumber}},
        {"uint64", {[](PropertyEditor& editor, const QString& initial) {
            return editor.buildText(initial, "[0-9]{1,20}", editor.rangeHint());
        }, sameNumber}},
        {"float", real},
        {"double", real},
        {"string", Style::text()},
        {"enum", {&PropertyEditor::buildEnum, sameEnum}},
        {"flags", {&PropertyEditor::buildFlags, sameFlags}},
        {"caps", Style::text()},
        {"fraction", {[](PropertyEditor& editor, const QString& initial) {
            return editor.buildText(initial, "-?[0-9]+/[0-9]+");
        }}},
    };
    const auto found = styles.find(kind);
    return found == styles.end() ? nullptr : &found->second;
}

const PropertyEditor::Style& PropertyEditor::styleFor(const std::string& kind) {
    const Style* style = supportedStyle(kind);
    return style ? *style : Style::text();
}

bool PropertyEditor::editable(const PropertyInfo& info) { return info.writable && supportedStyle(info.kind); }

PropertyEditor::PropertyEditor(const PropertyInfo& info, const std::optional<QString>& current, QWidget* parent)
    : QWidget(parent), info_(info), style_(styleFor(info.kind)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(style_.build(*this, current.value_or(fromStd(info.defaultValue))), 1);
    // A small ↺ beside a value that is not the factory default: it puts the default back (Save applies it).
    reset_ = new QToolButton(this);
    reset_->setObjectName("resetPropertyButton");
    reset_->setText("↺");
    reset_->setAutoRaise(true);
    reset_->setFixedSize(24, 24);
    reset_->setToolTip("Restore the factory default (" + fromStd(info.defaultValue) + "); Save applies the change");
    layout->addWidget(reset_);
    connect(reset_, &QToolButton::clicked, this, &PropertyEditor::resetToDefault);
    setToolTip(fromStd(info.blurb));
    refresh();
}

void PropertyEditor::resetToDefault() {
    auto* row = qobject_cast<QHBoxLayout*>(layout());
    auto* old = row->takeAt(0);
    delete old->widget();
    delete old;
    row->insertWidget(0, style_.build(*this, fromStd(info_.defaultValue)), 1);
    touch();
}

void PropertyEditor::touch() {
    touched_ = true;
    refresh();
    emit edited();
}

void PropertyEditor::refresh() { reset_->setVisible(!isDefault()); }

bool PropertyEditor::isDefault() const { return style_.matchesDefault(info_, text(), fromStd(info_.defaultValue)); }

gps::Props PropertyEditor::collect(const QList<PropertyEditor*>& editors, const gps::Props& original) {
    gps::Props out = original;
    for (const PropertyEditor* e : editors) {
        if (!e->touched()) continue;
        if (e->isDefault()) out.erase(e->info().name);
        else out[e->info().name] = toStd(e->text());
    }
    return out;
}

QWidget* PropertyEditor::buildBoolean(const QString& initial) {
    auto* box = new QCheckBox(this);
    box->setChecked(initial.toLower() == "true" || initial == "1");
    connect(box, &QCheckBox::toggled, this, &PropertyEditor::touch);
    read_ = [box] { return QString(box->isChecked() ? "true" : "false"); };
    return box;
}

QWidget* PropertyEditor::buildInt(const QString& initial) {
    auto* spin = new QSpinBox(this);
    const qlonglong lo = info_.min.empty() ? INT_MIN : qMax<qlonglong>(fromStd(info_.min).toLongLong(), INT_MIN);
    const qlonglong hi = info_.max.empty() ? INT_MAX : qMin<qlonglong>(fromStd(info_.max).toLongLong(), INT_MAX);
    spin->setRange(static_cast<int>(lo), static_cast<int>(hi));
    spin->setValue(initial.toInt());
    spin->setMinimumWidth(130);
    connect(spin, &QSpinBox::valueChanged, this, &PropertyEditor::touch);
    read_ = [spin] { return QString::number(spin->value()); };
    return spin;
}

QWidget* PropertyEditor::buildNumber(const QString& initial, double minimum, double maximum, int decimals, double step) {
    auto* spin = new QDoubleSpinBox(this);
    const double lo = info_.min.empty() ? minimum : qMax(fromStd(info_.min).toDouble(), minimum);
    const double hi = info_.max.empty() ? maximum : qMin(fromStd(info_.max).toDouble(), maximum);
    spin->setDecimals(decimals);
    spin->setRange(lo, hi);
    spin->setSingleStep(step);
    spin->setValue(initial.toDouble());
    spin->setMinimumWidth(130);
    connect(spin, &QDoubleSpinBox::valueChanged, this, &PropertyEditor::touch);
    read_ = [spin] { return QString::number(spin->value(), 'g', 10); };
    return spin;
}

QWidget* PropertyEditor::buildEnum(const QString& initial) {
    auto* combo = new QComboBox(this);
    for (const EnumValue& value : info_.enumValues) {
        combo->addItem(fromStd(value.nick + "  (" + value.value + ")"), fromStd(value.nick));
    }
    combo->setCurrentIndex(qMax(0, combo->findData(enumNick(info_, initial))));
    connect(combo, &QComboBox::currentIndexChanged, this, &PropertyEditor::touch);
    read_ = [combo] { return combo->currentData().toString(); };
    return combo;
}

QWidget* PropertyEditor::buildFlags(const QString& initial) {
    auto* box = new QWidget(this);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    const QStringList on = flagNicks(info_, initial);
    QList<QCheckBox*> checks;
    for (const EnumValue& value : info_.enumValues) {
        auto* check = new QCheckBox(fromStd(value.nick), box);
        check->setChecked(on.contains(fromStd(value.nick)));
        connect(check, &QCheckBox::toggled, this, &PropertyEditor::touch);
        column->addWidget(check);
        checks.append(check);
    }
    read_ = [checks = std::move(checks)] {
        QStringList nicks;
        for (const QCheckBox* check : checks) {
            if (check->isChecked()) nicks << check->text();
        }
        return nicks.isEmpty() ? QString("0") : nicks.join("+");
    };
    return box;
}

QWidget* PropertyEditor::buildText(const QString& initial, const QString& pattern, const QString& hint) {
    auto* line = new QLineEdit(initial, this);
    if (!pattern.isEmpty()) line->setValidator(new QRegularExpressionValidator(QRegularExpression(pattern), line));
    line->setToolTip(hint);
    connect(line, &QLineEdit::textEdited, this, &PropertyEditor::touch);
    read_ = [line] { return line->text(); };
    return line;
}

QString PropertyEditor::rangeHint() const { return QString("%1 … %2").arg(fromStd(info_.min), fromStd(info_.max)); }
