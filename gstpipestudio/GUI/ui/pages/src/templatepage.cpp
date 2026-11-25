// templatepage.cpp – see templatepage.h.
#include "templatepage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "contract.h"
#include "templates.h"
#include "theme.h"

namespace {
constexpr int kIndexRole = Qt::UserRole + 1;  // index into the choices
}

TemplatePage::TemplatePage(const TemplatePageInput& input, QWidget* parent) : DrawerPage(parent), input_(input) {
    setObjectName("templatePage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    auto* form = new QFormLayout;
    name_ = new QLineEdit(input.name, this);
    name_->setObjectName("templateName");
    name_->setPlaceholderText("Name of the template");
    form->addRow("Name", name_);
    root->addLayout(form);

    auto* hint = new QLabel("Parameters are what the collapsed template shows as its properties. Choose an element, "
                            "then one of its properties or a field of its fixed caps.", this);
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* pick = new QHBoxLayout;
    element_ = new QComboBox(this);
    element_->setObjectName("paramElement");
    for (const gps::ExposedParam& p : input.choices) {
        // A choice is named "<element as the user reads it>.<property>"; the combo keeps the unique name underneath.
        const QString name = fromStd(p.name), property = fromStd(p.property);
        if (element_->findData(fromStd(p.element)) < 0) element_->addItem(name.left(name.size() - property.size() - 1), fromStd(p.element));
    }
    property_ = new QComboBox(this);
    property_->setObjectName("paramProperty");
    property_->setMinimumContentsLength(18);
    auto* add = new QPushButton("Add parameter", this);
    add->setObjectName("addParameter");
    pick->addWidget(element_, 1);
    pick->addWidget(property_, 1);
    pick->addWidget(add);
    root->addLayout(pick);

    added_ = new QListWidget(this);
    added_->setObjectName("templateParams");
    root->addWidget(added_, 1);
    auto* remove = new QPushButton("Remove parameter", this);
    remove->setObjectName("removeParameter");
    root->addWidget(remove, 0, Qt::AlignLeft);

    error_ = new QLabel(this);
    error_->setObjectName("templateError");
    error_->setStyleSheet("color: " + Theme::error().name() + ";");
    error_->setWordWrap(true);
    error_->hide();
    root->addWidget(error_);
    addFooter(root, "Save template");

    connect(element_, &QComboBox::currentIndexChanged, this, [this] { fillProperties(); });
    connect(add, &QPushButton::clicked, this, [this] { this->add(); });
    connect(remove, &QPushButton::clicked, this, [this] { delete added_->currentItem(); });
    connect(primary_, &QPushButton::clicked, this, &TemplatePage::confirmed);
    name_->setReadOnly(input.fixedName);
    for (const gps::ExposedParam& p : input.chosen) {
        for (size_t i = 0; i < input_.choices.size(); ++i) {
            if (input_.choices[i].element != p.element || input_.choices[i].property != p.property) continue;
            input_.choices[i].name = p.name;  // the name it has
            auto* item = new QListWidgetItem(fromStd(p.name), added_);
            item->setData(kIndexRole, static_cast<int>(i));
        }
    }
    connect(name_, &QLineEdit::returnPressed, this, &TemplatePage::confirmed);
    fillProperties();
}

void TemplatePage::fillProperties() {
    property_->clear();
    const std::string element = toStd(element_->currentData().toString());
    for (size_t i = 0; i < input_.choices.size(); ++i) {
        const gps::ExposedParam& p = input_.choices[i];
        if (p.element == element) property_->addItem(fromStd(p.property), static_cast<int>(i));
    }
}

void TemplatePage::add() {
    if (property_->currentIndex() < 0) return;
    const int index = property_->currentData().toInt();
    for (int row = 0; row < added_->count(); ++row) {
        if (added_->item(row)->data(kIndexRole).toInt() == index) return;  // already a parameter
    }
    auto* item = new QListWidgetItem(fromStd(input_.choices[static_cast<size_t>(index)].name), added_);
    item->setData(kIndexRole, index);
}

QString TemplatePage::name() const { return name_->text().trimmed(); }

std::vector<gps::ExposedParam> TemplatePage::params() const {
    std::vector<gps::ExposedParam> out;
    for (int row = 0; row < added_->count(); ++row) out.push_back(input_.choices[static_cast<size_t>(added_->item(row)->data(kIndexRole).toInt())]);
    return out;
}

void TemplatePage::showError(const QString& text) {
    error_->setText(text);
    error_->setVisible(!text.isEmpty());
}

void TemplatePage::focusFirst() {
    name_->setFocus();
    name_->selectAll();
}
