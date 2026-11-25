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
    auto* heading = new QLabel(fromStd(input.info.entry.factory + "  —  " + input.info.entry.longName), this);
    heading->setObjectName("heading");
    heading->setWordWrap(true);
    titleCol->addWidget(heading);
    auto* sub = new QLabel(fromStd(input.info.entry.klass + (input.info.description.empty() ? "" : "  ·  " + input.info.description)), this);
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
        const QString name = fromStd(p.name);
        auto given = input.props.find(p.name);
        std::optional<QString> current;
        if (given != input.props.end()) current = fromStd(given->second);

        auto* row = new QWidget(list);
        row->setObjectName("row_" + name);
        auto* line = new QVBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(3);

        auto* label = new QLabel(p.deprecated ? name + "  (deprecated)" : name, row);
        label->setToolTip(fromStd(p.blurb));
        label->setWordWrap(true);
        if (p.deprecated) label->setStyleSheet("color: " + Theme::muted().name() + ";");
        else label->setStyleSheet("font-weight: bold;");

        auto* editor = new PropertyEditor(p, current, row);
        editor->setObjectName("editor_" + name);

        line->addWidget(label);
        line->addWidget(editor);
        rows->addWidget(row);
        editors_ << editor;
        rows_[name] = row;
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

    // Request pads, src and sink alike: how many to ask for before the run; never fewer than the linked ones.
    auto requestCounts = [&](const QString& title, const std::vector<PadTemplateInfo>& templates, const std::string& prefix,
                             const std::map<std::string, int>& linked, const std::map<std::string, int>& start,
                             QMap<QString, QSpinBox*>& spins) {
        auto* group = new QGroupBox(title, this);
        auto* form = new QVBoxLayout(group);
        auto countOf = [](const std::map<std::string, int>& counts, const std::string& name) {
            auto found = counts.find(name);
            return found == counts.end() ? 0 : found->second;
        };
        for (const PadTemplateInfo& t : templates) {
            if (!t.isRequest()) continue;
            auto* line = new QHBoxLayout;
            line->addWidget(new QLabel(fromStd(t.name), group));
            auto* spin = new QSpinBox(group);
            spin->setObjectName(fromStd(prefix + t.name));
            spin->setRange(countOf(linked, t.name), 32);
            spin->setValue(countOf(start, t.name));
            line->addWidget(spin);
            line->addStretch(1);
            form->addLayout(line);
            spins[fromStd(t.name)] = spin;
        }
        if (form->isEmpty()) {
            delete group;
            return;
        }
        sideCol->addWidget(group);
        hasSide = true;
    };
    requestCounts("Request src pads", input.info.srcTemplates(), "padCount_", input.linkedSrc, input.requestSrc, pads_);
    requestCounts("Request sink pads", input.info.sinkTemplates(), "sinkPadCount_", input.linkedSink, input.requestSink, sinkPadsCount_);

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

    QHBoxLayout* buttons = addFooter(root, input.acceptText);
    auto* reset = new QPushButton("Reset all properties", this);
    reset->setObjectName("resetPropertiesButton");
    buttons->insertWidget(0, reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        for (auto* editor : editors_) editor->resetToDefault();
    });
    if (input.canDelete) {
        auto* del = new QPushButton("Delete", this);
        del->setObjectName("deleteElementButton");
        del->setStyleSheet("QPushButton { background: #3b1c1e; border-color: #ff6369; color: #ff6369; font-weight: bold; }"
                           "QPushButton:hover { background: #ff6369; color: #111111; }");
        buttons->insertWidget(0, del);
        connect(del, &QPushButton::clicked, this, &PropertyPage::deleteRequested);
    }
    connect(primary_, &QPushButton::clicked, this, &PropertyPage::confirmed);

    connect(search_, &QLineEdit::textChanged, this, &PropertyPage::filter);
}

void PropertyPage::focusFirst() { search_->setFocus(); }

QString PropertyPage::sinkPad() const { return sinkPads_ ? sinkPads_->currentText() : QString(); }

void PropertyPage::filter(const QString& text) {
    for (const PropertyInfo& p : input_.info.properties) {
        QWidget* row = rows_.value(fromStd(p.name));
        if (!row) continue;
        row->setVisible(text.isEmpty() || fromStd(p.name).contains(text, Qt::CaseInsensitive) ||
                        fromStd(p.nick).contains(text, Qt::CaseInsensitive) || fromStd(p.blurb).contains(text, Qt::CaseInsensitive));
    }
}

ElementEdit PropertyPage::edit() const {
    ElementEdit e;
    e.props = PropertyEditor::collect(editors_, input_.props);
    auto counts = [](const QMap<QString, QSpinBox*>& spins, std::map<std::string, int>& out) {
        for (auto it = spins.begin(); it != spins.end(); ++it) {
            if (it.value()->value() > 0) out[toStd(it.key())] = it.value()->value();
        }
    };
    counts(pads_, e.requestSrc);
    counts(sinkPadsCount_, e.requestSink);
    return e;
}
