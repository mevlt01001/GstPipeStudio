// capsfieldeditor.cpp
#include "capsfieldeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <algorithm>
#include <climits>

CapsFieldEditor::CapsFieldEditor(const FieldInfo& field, QWidget* parent) : QWidget(parent), field_(field) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    if (field.kind == "list") {
        combo_ = new QComboBox(this);
        combo_->addItem("Auto", QString());
        for (const QString& v : field.values) combo_->addItem(v, v);
        layout->addWidget(combo_, 1);
        return;
    }

    pin_ = new QCheckBox("Pin", this);
    layout->addWidget(pin_);

    QWidget* input = nullptr;
    if (field.kind == "fixed") {
        QString value = field.values.value(0);
        input = new QLabel(value, this);
        read_ = [value] { return value; };
        write_ = [](const QString&) {};
    } else if (field.type == "gint") {
        auto* spin = new QSpinBox(this);
        auto clamped = [](const QString& text) { return static_cast<int>(std::clamp<qlonglong>(text.toLongLong(), INT_MIN, INT_MAX)); };
        spin->setRange(clamped(field.values.value(0)), clamped(field.values.value(1, "2147483647")));
        spin->setMinimumWidth(130);
        input = spin;
        read_ = [spin] { return QString::number(spin->value()); };
        write_ = [spin](const QString& v) { spin->setValue(v.toInt()); };
    } else if (field.type == "guint" || field.type == "gint64" || field.type == "guint64" || field.type == "gfloat" ||
               field.type == "gdouble") {
        bool real = field.type == "gfloat" || field.type == "gdouble";
        auto* spin = new QDoubleSpinBox(this);
        spin->setDecimals(real ? 3 : 0);
        spin->setRange(qMax(field.values.value(0).toDouble(), -9e15), qMin(field.values.value(1, "9e15").toDouble(), 9e15));
        spin->setMinimumWidth(130);
        input = spin;
        read_ = [spin, real] { return real ? QString::number(spin->value(), 'g', 10) : QString::number(spin->value(), 'f', 0); };
        write_ = [spin](const QString& v) { spin->setValue(v.toDouble()); };
    } else {
        auto* line = new QLineEdit(this);
        if (field.type == "GstFraction") {
            line->setValidator(new QRegularExpressionValidator(QRegularExpression("-?[0-9]+/[0-9]+"), line));
            line->setText(field.values.value(0));
            line->setToolTip("numerator/denominator, for example 30/1");
        }
        input = line;
        read_ = [line] { return line->text(); };
        write_ = [line](const QString& v) { line->setText(v); };
    }
    input->setEnabled(false);
    layout->addWidget(input, 1);
    connect(pin_, &QCheckBox::toggled, input, &QWidget::setEnabled);
}

std::optional<QString> CapsFieldEditor::pinned() const {
    if (combo_) {
        if (combo_->currentIndex() == 0) return std::nullopt;
        return combo_->currentData().toString();
    }
    if (!pin_->isChecked()) return std::nullopt;
    return read_();
}

void CapsFieldEditor::pin(const QString& value) {
    if (combo_) {
        int i = combo_->findData(value);
        if (i >= 0) combo_->setCurrentIndex(i);
        return;
    }
    write_(value);
    pin_->setChecked(true);
}
