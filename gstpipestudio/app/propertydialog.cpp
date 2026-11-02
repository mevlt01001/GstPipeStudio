// propertydialog.cpp
#include "propertydialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

#include "theme.h"

PropertyDialog::PropertyDialog(const PropertyDialogInput& input, QWidget* parent) : QDialog(parent), input_(input) {
    setWindowTitle(input.title);
    setObjectName("propertyDialog");
    resize(700, 620);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    auto* heading = new QLabel(input.info.factory + "  —  " + input.info.longName, this);
    heading->setObjectName("heading");
    root->addWidget(heading);
    auto* sub = new QLabel(input.info.klass + (input.info.description.isEmpty() ? "" : "  ·  " + input.info.description), this);
    sub->setObjectName("muted");
    sub->setWordWrap(true);
    root->addWidget(sub);

    auto* search = new QLineEdit(this);
    search->setObjectName("searchBox");
    search->setPlaceholderText("Property ara…");
    search->setClearButtonEnabled(true);
    root->addWidget(search);

    // One row per editable property. Rows are hidden, not removed, by the search box.
    auto* list = new QWidget;
    auto* rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 8, 0);
    rows->setSpacing(6);
    int shown = 0;
    for (const PropertyInfo& p : input.info.properties) {
        if (!PropertyEditor::editable(p)) continue;
        auto given = input.props.find(p.name.toStdString());
        std::optional<QString> current;
        if (given != input.props.end()) current = QString::fromStdString(given->second);

        auto* row = new QWidget(list);
        row->setObjectName("row_" + p.name);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);

        auto* label = new QLabel(p.deprecated ? p.name + "  (eski)" : p.name, row);
        label->setFixedWidth(200);
        label->setToolTip(p.blurb);
        label->setWordWrap(true);
        if (p.deprecated) label->setStyleSheet("color: " + Theme::muted().name() + ";");
        else label->setStyleSheet("font-weight: bold;");

        auto* editor = new PropertyEditor(p, current, row);
        editor->setObjectName("editor_" + p.name);

        auto* expose = new QCheckBox("dışarı aç", row);
        expose->setObjectName("expose_" + p.name);
        expose->setChecked(std::find(input.exposed.begin(), input.exposed.end(), p.name.toStdString()) != input.exposed.end());

        line->addWidget(label, 0, Qt::AlignTop);
        line->addWidget(editor, 1, Qt::AlignTop);
        line->addWidget(expose, 0, Qt::AlignTop);
        rows->addWidget(row);
        editors_ << editor;
        expose_[p.name] = expose;
        rows_[p.name] = row;
        ++shown;
    }
    if (shown == 0) {
        auto* none = new QLabel("Bu elementin düzenlenebilir property'si yok.", list);
        none->setObjectName("muted");
        rows->addWidget(none);
    }
    rows->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(list);
    root->addWidget(scroll, 1);

    // Request src pads: how many to ask for before the run.
    QList<PadTemplateInfo> requestSrc;
    for (const PadTemplateInfo& t : input.info.srcTemplates()) {
        if (t.isRequest()) requestSrc << t;
    }
    if (!requestSrc.isEmpty()) {
        auto* group = new QGroupBox("Request src pad sayısı", this);
        auto* form = new QVBoxLayout(group);
        for (const PadTemplateInfo& t : requestSrc) {
            auto* line = new QHBoxLayout;
            line->addWidget(new QLabel(t.name, group));
            auto* spin = new QSpinBox(group);
            spin->setObjectName("padCount_" + t.name);
            auto linked = input.linkedSrc.find(t.name.toStdString());
            auto start = input.requestSrc.find(t.name.toStdString());
            spin->setRange(linked == input.linkedSrc.end() ? 0 : linked->second, 32);
            spin->setValue(start == input.requestSrc.end() ? 0 : start->second);
            line->addWidget(spin);
            line->addStretch(1);
            form->addLayout(line);
            pads_[t.name] = spin;
        }
        root->addWidget(group);
    }

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* save = buttons->addButton("Kaydet", QDialogButtonBox::AcceptRole);
    save->setObjectName("saveButton");
    save->setDefault(true);
    QPushButton* cancel = buttons->addButton("İptal", QDialogButtonBox::RejectRole);
    cancel->setObjectName("cancelButton");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(search, &QLineEdit::textChanged, this, &PropertyDialog::filter);
}

void PropertyDialog::filter(const QString& text) {
    for (const PropertyInfo& p : input_.info.properties) {
        QWidget* row = rows_.value(p.name);
        if (!row) continue;
        row->setVisible(text.isEmpty() || p.name.contains(text, Qt::CaseInsensitive) ||
                        p.nick.contains(text, Qt::CaseInsensitive) || p.blurb.contains(text, Qt::CaseInsensitive));
    }
}

ElementEdit PropertyDialog::edit() const {
    ElementEdit e;
    e.props = PropertyEditor::collect(editors_, input_.props);
    for (auto it = pads_.begin(); it != pads_.end(); ++it) {
        if (it.value()->value() > 0) e.requestSrc[it.key().toStdString()] = it.value()->value();
    }
    for (auto it = expose_.begin(); it != expose_.end(); ++it) {
        if (it.value()->isChecked()) e.exposed.push_back(it.key().toStdString());
    }
    return e;
}
