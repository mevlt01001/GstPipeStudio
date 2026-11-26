// templateparamspage.cpp – see templateparamspage.h.
#include "templateparamspage.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "propertyeditor.h"
#include "templates.h"

TemplateParamsPage::TemplateParamsPage(const std::vector<TemplateParam>& params, QWidget* parent)
    : DrawerPage(parent), params_(params) {
    setObjectName("templateParamsPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    if (params.empty()) {
        auto* none = new QLabel("This template has no parameters. Expand it (+) to change its elements one by one.", this);
        none->setObjectName("muted");
        none->setWordWrap(true);
        root->addWidget(none);
    }
    auto* list = new QWidget;
    auto* form = new QFormLayout(list);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (const TemplateParam& p : params) {
        const QString name = fromStd(p.param.name);
        PropertyEditor* editor = nullptr;
        QLineEdit* text = nullptr;
        QWidget* field;
        if (p.property) {
            editor = new PropertyEditor(*p.property, p.current.isEmpty() ? std::nullopt : std::optional<QString>(p.current), list);
            field = editor;
        } else {
            text = new QLineEdit(p.current, list);
            text->setPlaceholderText("Auto");
            field = text;
        }
        field->setObjectName("param_" + name);
        auto* label = new QLabel(name, list);
        label->setToolTip(fromStd(p.param.element + " · " + p.param.property));
        form->addRow(label, field);
        editors_.push_back(editor);
        texts_.push_back(text);
    }
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(list);
    root->addWidget(scroll, 1);
    addFooter(root, "Save");
    primary_->setEnabled(!params.empty());
    connect(primary_, &QPushButton::clicked, this, &TemplateParamsPage::confirmed);
}

std::map<std::string, std::string> TemplateParamsPage::values() const {
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < params_.size(); ++i) {
        QString value;
        if (editors_[i]) value = editors_[i]->isDefault() ? QString() : editors_[i]->text();
        else value = texts_[i]->text().trimmed();
        out[params_[i].param.name] = toStd(value);
    }
    return out;
}

void TemplateParamsPage::focusFirst() {
    if (!editors_.empty() && editors_.front()) editors_.front()->setFocus();
    else if (!texts_.empty() && texts_.front()) texts_.front()->setFocus();
}
