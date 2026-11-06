// propertypage.cpp
#include "propertypage.h"

#include <QCheckBox>
#include <QComboBox>
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

PropertyPage::PropertyPage(const PropertyPageInput& input, QWidget* parent) : DrawerPage(parent), input_(input) {
    setObjectName("propertyPage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* topBar = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    auto* heading = new QLabel(input.info.factory + "  —  " + input.info.longName, this);
    heading->setObjectName("heading");
    heading->setWordWrap(true);
    titleCol->addWidget(heading);
    auto* sub = new QLabel(input.info.klass + (input.info.description.isEmpty() ? "" : "  ·  " + input.info.description), this);
    sub->setObjectName("muted");
    sub->setWordWrap(true);
    titleCol->addWidget(sub);
    topBar->addLayout(titleCol, 1);

    search_ = new QLineEdit(this);
    search_->setObjectName("searchBox");
    search_->setPlaceholderText("Search properties…");
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(320);
    topBar->addWidget(search_, 0, Qt::AlignTop);
    root->addLayout(topBar);

    auto* contentRow = new QHBoxLayout;
    contentRow->setSpacing(12);

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
        auto* line = new QVBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(3);

        auto* label = new QLabel(p.deprecated ? p.name + "  (deprecated)" : p.name, row);
        label->setToolTip(p.blurb);
        label->setWordWrap(true);
        if (p.deprecated) label->setStyleSheet("color: " + Theme::muted().name() + ";");
        else label->setStyleSheet("font-weight: bold;");

        auto* editor = new PropertyEditor(p, current, row);
        editor->setObjectName("editor_" + p.name);

        auto* expose = new QCheckBox("Expose", row);
        expose->setToolTip("Make this property a parameter of the saved pipeline (name=value on the gst-launch line)");
        expose->setObjectName("expose_" + p.name);
        expose->setChecked(std::find(input.exposed.begin(), input.exposed.end(), p.name.toStdString()) != input.exposed.end());

        auto* head = new QHBoxLayout;
        head->addWidget(label, 1);
        head->addWidget(expose, 0, Qt::AlignRight);
        line->addLayout(head);
        line->addWidget(editor);
        rows->addWidget(row);
        editors_ << editor;
        expose_[p.name] = expose;
        rows_[p.name] = row;
        ++shown;
    }
    if (shown == 0) {
        auto* none = new QLabel("This element has no editable properties.", list);
        none->setObjectName("muted");
        rows->addWidget(none);
    }
    rows->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(list);
    contentRow->addWidget(scroll, 3);

    auto* sideCol = new QVBoxLayout;
    sideCol->setSpacing(6);
    bool hasSide = false;

    // Request src pads: how many to ask for before the run.
    QList<PadTemplateInfo> requestSrc;
    for (const PadTemplateInfo& t : input.info.srcTemplates()) {
        if (t.isRequest()) requestSrc << t;
    }
    if (!requestSrc.isEmpty()) {
        auto* group = new QGroupBox("Request src pads", this);
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
        sideCol->addWidget(group);
        hasSide = true;
    }

    // Request sink pads: how many to ask for before the run.
    QList<PadTemplateInfo> requestSink;
    for (const PadTemplateInfo& t : input.info.sinkTemplates()) {
        if (t.isRequest()) requestSink << t;
    }
    if (!requestSink.isEmpty()) {
        auto* group = new QGroupBox("Request sink pads", this);
        auto* form = new QVBoxLayout(group);
        for (const PadTemplateInfo& t : requestSink) {
            auto* line = new QHBoxLayout;
            line->addWidget(new QLabel(t.name, group));
            auto* spin = new QSpinBox(group);
            spin->setObjectName("sinkPadCount_" + t.name);
            auto linked = input.linkedSink.find(t.name.toStdString());
            auto start = input.requestSink.find(t.name.toStdString());
            spin->setRange(linked == input.linkedSink.end() ? 0 : linked->second, 32);
            spin->setValue(start == input.requestSink.end() ? 0 : start->second);
            line->addWidget(spin);
            line->addStretch(1);
            form->addLayout(line);
            sinkPadsCount_[t.name] = spin;
        }
        sideCol->addWidget(group);
        hasSide = true;
    }

    if (input.sinkPads.size() > 1) {
        auto* group = new QGroupBox("Connect to sink pad", this);
        auto* form = new QVBoxLayout(group);
        sinkPads_ = new QComboBox(group);
        sinkPads_->setObjectName("sinkPadCombo");
        sinkPads_->addItems(input.sinkPads);
        form->addWidget(sinkPads_);
        sideCol->addWidget(group);
        hasSide = true;
    }
    if (hasSide) {
        sideCol->addStretch(1);
        contentRow->addLayout(sideCol, 1);
    } else {
        delete sideCol;
    }
    root->addLayout(contentRow, 1);

    auto* buttons = new QHBoxLayout;
    if (input.canDelete) {
        auto* del = new QPushButton("Delete", this);
        del->setObjectName("deleteElementButton");
        del->setStyleSheet("QPushButton { background: #3b1c1e; border-color: #ff6369; color: #ff6369; font-weight: bold; }"
                           "QPushButton:hover { background: #ff6369; color: #111111; }");
        buttons->addWidget(del);
        connect(del, &QPushButton::clicked, this, &PropertyPage::deleteRequested);
    }
    buttons->addStretch(1);
    auto* cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancelButton");
    primary_ = new QPushButton(input.acceptText, this);
    primary_->setObjectName("saveButton");
    buttons->addWidget(cancel);
    buttons->addWidget(primary_);
    root->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, this, &DrawerPage::cancelled);
    connect(primary_, &QPushButton::clicked, this, &PropertyPage::confirmed);

    connect(search_, &QLineEdit::textChanged, this, &PropertyPage::filter);
}

void PropertyPage::focusFirst() { search_->setFocus(); }

QString PropertyPage::sinkPad() const { return sinkPads_ ? sinkPads_->currentText() : QString(); }

void PropertyPage::filter(const QString& text) {
    for (const PropertyInfo& p : input_.info.properties) {
        QWidget* row = rows_.value(p.name);
        if (!row) continue;
        row->setVisible(text.isEmpty() || p.name.contains(text, Qt::CaseInsensitive) ||
                        p.nick.contains(text, Qt::CaseInsensitive) || p.blurb.contains(text, Qt::CaseInsensitive));
    }
}

ElementEdit PropertyPage::edit() const {
    ElementEdit e;
    e.props = PropertyEditor::collect(editors_, input_.props);
    for (auto it = pads_.begin(); it != pads_.end(); ++it) {
        if (it.value()->value() > 0) e.requestSrc[it.key().toStdString()] = it.value()->value();
    }
    for (auto it = sinkPadsCount_.begin(); it != sinkPadsCount_.end(); ++it) {
        if (it.value()->value() > 0) e.requestSink[it.key().toStdString()] = it.value()->value();
    }
    for (auto it = expose_.begin(); it != expose_.end(); ++it) {
        if (it.value()->isChecked()) e.exposed.push_back(it.key().toStdString());
    }
    return e;
}
