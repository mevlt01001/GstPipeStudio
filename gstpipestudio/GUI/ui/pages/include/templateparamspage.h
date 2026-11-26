// templateparamspage.h – the Properties tab of a placed template (TEMPLATE_PLAN D9): exactly its parameters, edited as
// if the template were one element. A property parameter gets the editor its property has on the property page; a
// caps field gets a text box. Parameters it was not given stay inside: expand the template to reach them.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "contract.h"
#include "drawer.h"
#include "graph.h"

class PropertyEditor;
class QLineEdit;

struct TemplateParam {
    gps::ExposedParam param;
    std::optional<PropertyInfo> property;  // none: a caps field
    QString current;                       // "" = the default
};

class TemplateParamsPage : public DrawerPage {
    Q_OBJECT
public:
    explicit TemplateParamsPage(const std::vector<TemplateParam>& params, QWidget* parent = nullptr);
    std::map<std::string, std::string> values() const;  // parameter name -> value, "" = default
    void focusFirst() override;

signals:
    void confirmed();

private:
    std::vector<TemplateParam> params_;
    std::vector<PropertyEditor*> editors_;  // per parameter, nullptr for a caps field
    std::vector<QLineEdit*> texts_;         // per parameter, nullptr for a property
};
