// templatepage.h – the page that names a new template and chooses its parameters (TEMPLATE_PLAN D5, D11). The elements
// are already set up, so it lists no properties: Add parameter picks an element, then one of its properties or a field
// of its fixed caps ("element.property", "element.caps.field"). Those become what the collapsed template shows.
#pragma once

#include <QStringList>
#include <vector>

#include "drawer.h"
#include "graph.h"
#include "names.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

struct TemplatePageInput {
    QString name;                               // suggested
    std::vector<gps::ExposedParam> choices;     // every parameter Add parameter may offer
    std::vector<gps::ExposedParam> chosen = {}; // parameters it has already
    bool fixedName = false;                     // the name is shown, not asked
    gps::Names names = {};                      // what the elements are called in the element list
};

class TemplatePage : public DrawerPage {
    Q_OBJECT
public:
    explicit TemplatePage(const TemplatePageInput& input, QWidget* parent = nullptr);

    QString name() const;
    std::vector<gps::ExposedParam> params() const;  // in the order they were added
    void showError(const QString& text);
    void focusFirst() override;

signals:
    void confirmed();

private:
    void fillProperties();
    void add();

    TemplatePageInput input_;
    QLineEdit* name_;
    QComboBox* element_;
    QComboBox* property_;
    QListWidget* added_;
    QLabel* error_;
};
