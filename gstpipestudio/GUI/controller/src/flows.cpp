// flows.cpp
#include "flows.h"

#include <algorithm>
#include <memory>

#include "capspage.h"
#include "capstext.h"
#include "drawer.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "templatepage.h"
#include "templateparamspage.h"
#include "templates.h"

Flows::Flows(Controller* controller, Drawer* drawer, QWidget* window)
    : QObject(window), controller_(controller), drawer_(drawer), window_(window) {}

void Flows::setBottomPanels(Panels* panels, QWidget* propsTab, QWidget* capsTab, QWidget* defaultTab) {
    props_.attach(panels, propsTab, defaultTab);
    caps_.attach(panels, capsTab, defaultTab);
    closeProperties();
    closeCaps();
}

void Flows::setBusy(bool busy) {
    if (PropertyPage* p = propertyPage()) p->setEnabled(!busy);
    if (CapsPage* c = capsPage()) c->setEnabled(!busy);
}

void Flows::saveAsTemplate(const QStringList& elements) {
    const QString id = controller_->beginTemplate(elements);
    if (id.isEmpty()) return;
    TemplatePageInput input;
    input.choices = controller_->parameterChoices(controller_->outline().membersOf(id));
    input.names = controller_->names();
    auto* page = new TemplatePage(input);
    auto saved = std::make_shared<bool>(false);
    connect(page, &TemplatePage::confirmed, this, [this, page, id, saved] {
        QString error;
        if (!controller_->finishTemplate(id, page->name(), page->params(), &error)) {
            page->showError(error);
            return;
        }
        *saved = true;
        closeProperties();
    });
    connect(page, &QObject::destroyed, this, [this, id, saved] {
        if (!*saved) controller_->cancelTemplate(id);
    });
    presentProperties(page, "Save as template");
}

QString Flows::path(const QString& element, const QString& identity) const {
    return path(gps::PadRef{toStd(element), toStd(identity), gps::Pad::Direction::Source});
}

QString Flows::path(const gps::PadRef& pad) const { return controller_->padName(pad); }

QString Flows::title(const QString& element) const { return fromStd(controller_->names().element(toStd(element))); }

void Flows::editTemplateParams(const QString& instanceId) {
    const gps::TemplateInstance* t = controller_->plan().instance(toStd(instanceId));
    if (!t || controller_->editingBlocked()) return;
    std::vector<TemplateParam> rows;
    for (const gps::ExposedParam& p : t->params) {
        TemplateParam row{p, std::nullopt, fromStd(gps::templates::paramValue(controller_->plan(), p))};
        const gps::Element* e = controller_->plan().find(p.element);
        if (const ElementInfo* info = e ? catalog().info(fromStd(e->factory)) : nullptr) {
            for (const PropertyInfo& property : info->properties) {
                if (property.name == p.property) row.property = property;
            }
        }
        rows.push_back(row);
    }
    auto* page = new TemplateParamsPage(rows);
    connect(page, &TemplateParamsPage::confirmed, this, [this, page, instanceId] {
        if (controller_->setTemplateParams(instanceId, page->values())) closeProperties();
    });
    presentProperties(page, "Template · " + fromStd(controller_->names().instance(t->id)));
}

PropertyPage* Flows::propertyPage() const {
    return props_.tab() ? props_.tab()->findChild<PropertyPage*>() : nullptr;
}

CapsPage* Flows::capsPage() const {
    return caps_.tab() ? caps_.tab()->findChild<CapsPage*>() : nullptr;
}

void Flows::presentProperties(DrawerPage* page, const QString& title) {
    drawer_->dismiss();
    if (props_.present(page, title)) connect(page, &DrawerPage::cancelled, this, &Flows::closeProperties);
}

void Flows::closeProperties() {
    abandonPending();  // closing while a validation runs: no caps step afterwards
    props_.close();
}

void Flows::presentCaps(CapsPage* page, const QString& title, const QString& subtitle) {
    drawer_->dismiss();
    if (!caps_.present(page, title, subtitle)) return;
    connect(page, &DrawerPage::cancelled, this, &Flows::closeCaps);
    connect(page, &CapsPage::skipped, this, &Flows::closeCaps);
}

void Flows::closeCaps() { caps_.close(); }

// The user closed the drawer or cancelled while the validation of an edit was still running: no caps page opens afterwards.
void Flows::abandonPending() { disconnect(pending_); }

// ---- Add element (Right Drawer ONLY presents PickerPage)

std::vector<FactoryEntry> Flows::factories(bool sources) const {
    std::vector<FactoryEntry> out;
    for (const FactoryEntry& e : catalog().entries()) {
        if (e.isSource == sources) out.push_back(e);
    }
    return out;
}

void Flows::chooseElement() {
    if (controller_->editingBlocked()) return;
    PickerData data;
    data.newHeader = "Elements";
    data.showFilters = true;
    data.candidates = catalog().entries();
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) {
        newElementProperties(choice.factory);
    });
    drawer_->present(page, "Add element", "Choose an element to place on the canvas: a source, a filter or a sink.", Drawer::Replace);
}

void Flows::chooseComponent() {
    if (controller_->editingBlocked()) return;
    PickerData data;
    data.providers = providers();
    data.componentMenu = [this](const ComponentRef& component, const QPoint& at) { emit componentMenuRequested(component, at); };
    for (ComponentProvider* provider : controller_->services().providers()) {
        if (!provider->canImport()) continue;
        data.importComponent = [this, kind = provider->kind()] { emit importRequested(kind); };
        break;
    }
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) {
        if (choice.kind != PickerChoice::Component) return;
        drawer_->dismiss();
        controller_->place(choice.component);
    });
    // Renamed, deleted or imported from anywhere: the list is shown again, with it.
    for (ComponentProvider* provider : controller_->services().providers()) {
        connect(provider, &ComponentProvider::changed, page, [this, page] {
            if (drawer_->page() == page) chooseComponent();
        }, Qt::QueuedConnection);
    }
    const bool none = std::all_of(data.providers.begin(), data.providers.end(), [](const ComponentProvider* p) { return p->components().isEmpty(); });
    drawer_->present(page, "Add template",
                     none ? "No templates yet: select elements and use Save as template."
                          : "Choose a saved template; hover one for a quick look inside.",
                     Drawer::Replace);
}

QList<const ComponentProvider*> Flows::providers() const {
    QList<const ComponentProvider*> out;
    for (const ComponentProvider* p : controller_->services().providers()) out << p;
    return out;
}

// The template being edited on the canvas: its parameters, chosen like when it was saved.
void Flows::editTemplateParameters() {
    TemplatePageInput input;
    input.name = controller_->target().libraryTemplate();
    input.choices = controller_->parameterChoices(fromStd(controller_->plan().elementNames()));
    input.chosen = controller_->plan().exposed;
    input.fixedName = true;
    input.names = controller_->names();
    auto* page = new TemplatePage(input);
    connect(page, &TemplatePage::confirmed, this, [this, page] {
        controller_->setTemplateParameters(page->params());
        closeProperties();
    });
    presentProperties(page, "Template parameters · " + controller_->target().libraryTemplate());
}

void Flows::newElementProperties(const QString& factory) {
    catalog().fetchInfo(factory, this, [this, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyPageInput in;
        in.info = *info;
        in.requestSrc = info->defaultRequestSrc();
        in.requestSink = info->defaultRequestSink();
        auto* page = new PropertyPage(in);
        connect(page, &PropertyPage::confirmed, this, [this, page, factory] {
            if (controller_->addElement(factory, page->edit())) closeProperties();
        });
        presentProperties(page, "Add element · " + factory);
    });
}

// ---- left click on a free src pad (Right Drawer ONLY presents PickerPage)

void Flows::fromFreePad(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
    QString allowed = controller_->padAllowed(element, identity);
    catalog().fetchCompatible(allowed, this, [this, element, identity, allowed](const QStringList& compatible) {
        PickerData data;
        data.existing = controller_->existingTargets(element, identity);
        data.providers = providers();
        data.offers = [](const ComponentInfo& c) { return c.takesInput; };
        data.compatible = compatible;
        data.candidates = factories(false);
        auto* page = new PickerPage(data);
        connect(page, &PickerPage::picked, this, [this, element, identity](const PickerChoice& choice) {
            if (choice.kind == PickerChoice::Existing) {
                drawer_->dismiss();
                editThenCaps(element, identity, [=] {
                    return controller_->linkExisting(element, identity, choice.existing.element, choice.existing.sinkPad);
                });
            } else if (choice.kind == PickerChoice::Component) {
                drawer_->dismiss();
                editThenCaps(element, identity, [=] {
                    return !controller_->placeAfter(element, identity, choice.component).isEmpty();
                });
            } else {
                newElement(Mode::Add, element, identity, choice.factory);
            }
        });
        drawer_->present(page, "Add after " + title(element) + " · " + identity, allowed.isEmpty() ? QString() : fromStd(gps::mediaOf(toStd(allowed))), Drawer::Replace);
    });
}

void Flows::newElement(Mode mode, const QString& element, const QString& identity, const QString& factory) {
    catalog().fetchInfo(factory, this, [this, mode, element, identity, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyPageInput in;
        in.info = *info;
        in.acceptText = mode == Mode::Add ? "Add element" : "Replace";
        in.requestSrc = info->defaultRequestSrc();
        std::vector<PadTemplateInfo> choices = controller_->sinkChoices(*info, element, identity);
        if (choices.empty()) return;
        for (const PadTemplateInfo& t : choices) in.sinkPads << fromStd(t.name);
        auto* page = new PropertyPage(in);
        connect(page, &PropertyPage::confirmed, this, [this, mode, page, element, identity, factory, onlyPad = fromStd(choices.front().name)] {
            ElementEdit edit = page->edit();
            QString sinkPad = page->sinkPad().isEmpty() ? onlyPad : page->sinkPad();
            editThenCaps(element, identity, [=] {
                return mode == Mode::Add ? controller_->addAfter(element, identity, factory, sinkPad, edit)
                                         : controller_->replace(element, identity, factory, sinkPad, edit);
            });
        });
        QString title = (mode == Mode::Add ? "Add after " : "Replace after ") + path(element, identity) + " · " + factory;
        presentProperties(page, title);
    });
}

// Runs an edit; once its validation ends the Properties tab closes and, on success, the Caps tab opens for the link.
void Flows::editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(controller_, &Controller::updateFinished, this, [this, connection, element, identity](bool ok) {
        disconnect(*connection);
        closeProperties();
        if (ok) openCaps(element, identity, true);
    });
    pending_ = *connection;
    if (!edit()) disconnect(*connection);  // refused: the page stays as it is
}

void Flows::openCaps(const QString& element, const QString& identity, bool afterEdit) {
    int link = controller_->plan().linkFrom(element.toStdString(), identity.toStdString());
    if (link < 0) {
        closeCaps();
        return;
    }
    CapsPageInput in;
    const gps::Connection& l = controller_->plan().links[link];
    in.allowed = controller_->padStructures(element, identity);
    in.current = fromStd(l.caps);
    in.skippable = afterEdit;
    if (const PadTemplateInfo* sink = controller_->linkSinkTemplate(link)) {
        for (const PropertyInfo& p : sink->padProperties) {
            if (PropertyEditor::editable(p)) in.padProperties.push_back(p);
        }
    }
    in.padProps = l.sinkPadProps;
    if (afterEdit && in.allowed.empty() && in.padProperties.empty()) {  // nothing to choose
        return;
    }

    auto* page = new CapsPage(in);
    connect(page, &CapsPage::applied, this, [this, page, element, identity] {
        controller_->setCaps(element, identity, page->caps(), page->padProps());
        closeCaps();
    });
    QString title = (afterEdit ? "Caps · " : "Edit caps · ") + path(element, identity);
    QString sub = path(element, identity) + "  →  " + path(controller_->plan().sinkOf(static_cast<size_t>(link)));
    presentCaps(page, title, sub);
}

// ---- link and element actions

void Flows::editCaps(const QString& element, const QString& identity) {
    if (!controller_->editingBlocked()) openCaps(element, identity, false);
}

void Flows::replaceBehind(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
    QString allowed = controller_->padAllowed(element, identity);
    catalog().fetchCompatible(allowed, this, [this, element, identity](const QStringList& compatible) {
        PickerData data;
        data.compatible = compatible;
        data.candidates = factories(false);
        auto* page = new PickerPage(data);
        connect(page, &PickerPage::picked, this, [this, element, identity](const PickerChoice& choice) {
            newElement(Mode::Replace, element, identity, choice.factory);
        });
        drawer_->present(page, "Replace after " + path(element, identity), "Choose the element that takes the place of the one behind this pad.", Drawer::Replace);
    });
}

// The edit itself is refused while editing is blocked.
void Flows::addQueue(const QString& element, const QString& identity) {
    catalog().fetchInfo(gps::kQueueFactory, this, [this, element, identity](const ElementInfo* info) {
        if (info) controller_->insertQueue(element, identity);
    });
}

void Flows::editProperties(const QString& element) {
    if (controller_->editingBlocked()) return;
    const QString grp = controller_->outline().collapsedTemplate(element);
    if (!grp.isEmpty()) {
        controller_->setTemplateExpanded(grp, true);
        return;
    }
    const gps::Element* e = controller_->plan().find(element.toStdString());
    const ElementInfo* info = e ? catalog().info(fromStd(e->factory)) : nullptr;
    if (!info) return;

    PropertyPageInput in;
    in.info = *info;
    in.acceptText = "Save";
    in.canDelete = true;
    in.props = e->props;
    in.requestSrc = e->requestSrc;
    in.requestSink = e->requestSink;
    in.linkedSrc = controller_->plan().linkedRequestPads(e->name, gps::Pad::Direction::Source);
    in.linkedSink = controller_->plan().linkedRequestPads(e->name, gps::Pad::Direction::Sink);
    auto* page = new PropertyPage(in);
    connect(page, &PropertyPage::confirmed, this, [this, page, element] {
        if (controller_->editElement(element, page->edit())) closeProperties();
    });
    connect(page, &PropertyPage::deleteRequested, this, [this, element] {
        closeProperties();
        controller_->deleteElement(element);
    });
    presentProperties(page, "Edit properties · " + title(element));
}


