// flows.cpp
#include "flows.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include "panels.h"

#include <QAction>
#include <algorithm>
#include <memory>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTabWidget>
#include <QVBoxLayout>

#include "capspage.h"
#include "capstext.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "templatepage.h"
#include "templateparamspage.h"
#include "templates.h"
#include "theme.h"

namespace {


QString pluralElements(int n) { return QString("%1 element%2").arg(n).arg(n == 1 ? "" : "s"); }

void clearTabLayout(QWidget* tab) {
    if (!tab || !tab->layout()) return;
    QLayout* layout = tab->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
}

}  // namespace

Flows::Flows(Controller* controller, Drawer* drawer, QWidget* window)
    : QObject(window), controller_(controller), drawer_(drawer), window_(window) {}

void Flows::setBottomPanels(Panels* panels, QWidget* propsTab, QWidget* capsTab, QWidget* defaultTab) {
    panels_ = panels;
    propsTab_ = propsTab;
    capsTab_ = capsTab;
    defaultTab_ = defaultTab;
    closeProperties();
    closeCaps();
}

void Flows::setBusy(bool busy) {
    if (PropertyPage* p = propertyPage()) p->setEnabled(!busy);
    if (CapsPage* c = capsPage()) c->setEnabled(!busy);
}

void Flows::saveAsTemplate(const QStringList& elements) {
    if (controller_->editingBlocked() || elements.isEmpty()) return;
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
    presentProperties(page, "Template · " + fromStd(t->templateName));
}

PropertyPage* Flows::propertyPage() const {
    return propsTab_ ? propsTab_->findChild<PropertyPage*>() : nullptr;
}

CapsPage* Flows::capsPage() const {
    return capsTab_ ? capsTab_->findChild<CapsPage*>() : nullptr;
}

void Flows::presentProperties(DrawerPage* page, const QString& title) {
    drawer_->dismiss();
    if (!propsTab_ || !panels_) {
        page->deleteLater();
        return;
    }
    clearTabLayout(propsTab_);
    propsTitle_ = title;
    auto* layout = static_cast<QVBoxLayout*>(propsTab_->layout());
    auto* titleLabel = new QLabel(title, propsTab_);
    titleLabel->setObjectName("propsTabTitle");
    titleLabel->setStyleSheet("font-weight: bold; color: " + Theme::accent().name() + ";");
    layout->addWidget(titleLabel);
    page->setParent(propsTab_);
    layout->addWidget(page, 1);
    page->show();
    connect(page, &DrawerPage::cancelled, this, &Flows::closeProperties);
    panels_->show(propsTab_);
    page->focusFirst();
}

void Flows::closeProperties() {
    abandonPending();  // closing while a validation runs: no caps step afterwards
    propsTitle_.clear();
    if (!propsTab_) return;
    bool wasCurrent = panels_ && panels_->showing(propsTab_);
    clearTabLayout(propsTab_);
    auto* layout = static_cast<QVBoxLayout*>(propsTab_->layout());
    auto* hint = new QLabel("Click an element on the canvas to inspect or edit its Properties.", propsTab_);
    hint->setObjectName("muted");
    layout->addWidget(hint);
    layout->addStretch(1);
    if (wasCurrent && defaultTab_) panels_->show(defaultTab_);
}

void Flows::presentCaps(CapsPage* page, const QString& title, const QString& subtitle) {
    drawer_->dismiss();
    if (!capsTab_ || !panels_) {
        page->deleteLater();
        return;
    }
    clearTabLayout(capsTab_);
    capsTitle_ = title;
    auto* layout = static_cast<QVBoxLayout*>(capsTab_->layout());
    auto* headRow = new QHBoxLayout;
    auto* titleLabel = new QLabel(title, capsTab_);
    titleLabel->setObjectName("capsTabTitle");
    titleLabel->setStyleSheet("font-weight: bold; color: " + Theme::accent().name() + ";");
    headRow->addWidget(titleLabel);
    if (!subtitle.isEmpty()) {
        auto* subLabel = new QLabel(subtitle, capsTab_);
        subLabel->setObjectName("capsTabSubtitle");
        subLabel->setStyleSheet("color: " + Theme::muted().name() + ";");
        headRow->addWidget(subLabel);
    }
    headRow->addStretch(1);
    auto* headWidget = new QWidget(capsTab_);
    headWidget->setLayout(headRow);
    headRow->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(headWidget);
    page->setParent(capsTab_);
    layout->addWidget(page, 1);
    page->show();
    connect(page, &DrawerPage::cancelled, this, &Flows::closeCaps);
    connect(page, &CapsPage::skipped, this, &Flows::closeCaps);
    panels_->show(capsTab_);
    page->focusFirst();
}

void Flows::closeCaps() {
    capsTitle_.clear();
    if (!capsTab_) return;
    bool wasCurrent = panels_ && panels_->showing(capsTab_);
    clearTabLayout(capsTab_);
    auto* layout = static_cast<QVBoxLayout*>(capsTab_->layout());
    auto* hint = new QLabel("Click a link on the canvas to inspect or edit its Caps.", capsTab_);
    hint->setObjectName("muted");
    layout->addWidget(hint);
    layout->addStretch(1);
    if (wasCurrent && defaultTab_) panels_->show(defaultTab_);
}

// The user closed the drawer or cancelled while the validation of an edit was still running: no caps page opens afterwards.
void Flows::abandonPending() { disconnect(pending_); }

void Flows::confirmRemoval(const gps::Removal& removal, const QString& verb, std::function<void()> accepted) {
    if (removal.elements.empty()) {
        accepted();
        return;
    }
    QStringList lines;
    for (const std::string& name : removal.elements) {
        const gps::Element* e = controller_->plan().find(name);
        lines << fromStd(name) + (e ? "   (" + fromStd(e->factory) + ")" : QString());
    }
    int n = static_cast<int>(removal.elements.size());
    QString heading = QString("These %1 will be removed, with %2 link%3").arg(pluralElements(n)).arg(removal.links).arg(removal.links == 1 ? "" : "s");
    auto* dialog = new ConfirmDialog("Remove elements", heading, lines, verb + " " + pluralElements(n), window_);
    connect(dialog, &QDialog::accepted, this, std::move(accepted));
    connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
    dialog->open();
}

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
    data.componentMenu = [this](const ComponentRef& component, const QPoint& at) { componentMenu(component, at); };
    for (ComponentProvider* provider : controller_->services().providers()) {
        if (!provider->canImport()) continue;
        data.importComponent = [this, provider] { importInto(*provider); };
        break;
    }
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) {
        if (choice.kind != PickerChoice::Component) return;
        drawer_->dismiss();
        controller_->place(choice.component);
    });
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

void Flows::importInto(ComponentProvider& provider) {
    const QString file = QFileDialog::getOpenFileName(window_, "Import " + provider.kind(), QDir::homePath(), "Templates (*.conf)");
    if (file.isEmpty()) return;
    QString error;
    if (provider.importFrom(file, &error).isEmpty()) QMessageBox::warning(window_, "Import " + provider.kind(), error);
    else chooseComponent();  // the list again, with it
}

// A component's own menu, from what its provider offers: edit it in its own window, rename, export or delete it.
void Flows::componentMenu(const ComponentRef& component, const QPoint& at) {
    ComponentProvider* provider = controller_->services().provider(component.provider);
    if (!provider) return;
    const QString kind = provider->kind(), name = component.id;
    QMenu menu(window_);
    for (ComponentAction action : provider->actions(name)) {
        switch (action) {
        case ComponentAction::Edit:
            menu.addAction("Edit " + kind, this, [this, component] {
                drawer_->dismiss();
                emit editComponentRequested(component);
            })->setObjectName("actionEditLibraryTemplate");
            break;
        case ComponentAction::Rename:
            menu.addAction("Rename…", this, [this, component, kind, name] {
                bool ok = false;
                const QString to = QInputDialog::getText(window_, "Rename " + kind, "New name of " + name, QLineEdit::Normal, name, &ok);
                QString error;
                if (!ok) return;
                if (!controller_->renameComponent(component, to, &error)) QMessageBox::warning(window_, "Rename " + kind, error);
                else chooseComponent();
            });
            break;
        case ComponentAction::Export:
            menu.addAction("Export…", this, [this, provider, kind, name] {
                const QString file = QFileDialog::getSaveFileName(window_, "Export " + kind, QDir::home().filePath(name + ".conf"), "Templates (*.conf)");
                QString error;
                if (!file.isEmpty() && !provider->exportTo(name, file, &error)) QMessageBox::warning(window_, "Export " + kind, error);
            });
            break;
        case ComponentAction::Delete:
            menu.addSeparator();
            menu.addAction("Delete…", this, [this, provider, kind, name] {
                const QString note = provider->removalNote(name);
                if (QMessageBox::question(window_, "Delete " + kind, "Delete the " + kind + " " + name + " from the library?" + (note.isEmpty() ? QString() : "\n\n" + note),
                                          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
                    return;
                provider->remove(name);
                chooseComponent();
            })->setObjectName("actionDeleteLibraryTemplate");
            break;
        }
    }
    if (!menu.isEmpty()) menu.exec(at);
}

// The template being edited on the canvas: its parameters, chosen like when it was saved.
void Flows::editTemplateParameters() {
    QStringList all;
    for (const gps::Element& e : controller_->plan().elements) all << fromStd(e.name);
    TemplatePageInput input;
    input.name = controller_->target().libraryTemplate();
    input.choices = controller_->parameterChoices(all);
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

// ---- menus

void Flows::showLinkMenu(const QString& element, const QString& identity, const QPoint& at) {
    if (controller_->editingBlocked()) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("linkMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Edit caps…", this, [=] { editCaps(element, identity); })->setObjectName("actionEditCaps");
    menu->addAction("Replace element…", this, [=] { replaceBehind(element, identity); })->setObjectName("actionReplace");
    menu->addAction("Cut link", this, [=] { cutLink(element, identity); })->setObjectName("actionCut");
    menu->addAction("Insert queue", this, [=] { addQueue(element, identity); })->setObjectName("actionInsertQueue");
    menu->popup(at);
}

void Flows::showElementMenu(const QString& element, const QPoint& at) {
    if (controller_->editingBlocked()) return;
    const gps::Element* e = controller_->plan().find(element.toStdString());
    if (!e) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("elementMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Edit properties…", this, [=] { editProperties(element); })->setObjectName("actionProperties");
    menu->addAction("Copy element", this, [=] {
        if (auto* clipboard = QGuiApplication::clipboard())
            clipboard->setText(QString::fromStdString(controller_->copySubplan({element}).toConf()));
    })->setObjectName("actionCopyElement");
    menu->addAction("Duplicate element", this, [=] { controller_->duplicateElements({element}); })->setObjectName("actionDuplicateElement");
    menu->addAction("Paste", this, [=] {
        auto* clipboard = QGuiApplication::clipboard();
        if (!clipboard) return;
        gps::Graph graph; std::string error;
        if (gps::Graph::fromConf(clipboard->text().toStdString(), &graph, &error)) controller_->pasteSubplan(graph);
    })->setObjectName("actionPasteElement");
    if (controller_->view(element).isSource) {
        menu->addAction("Delete source", this, [=] { deleteSource(element); })->setObjectName("actionDelete");
    } else {
        menu->addAction("Delete element", this, [=] { deleteElementOnly(element); })->setObjectName("actionDeleteElement");
    }
    if (e->factory == gps::kQueueFactory) {
        menu->addAction("Remove queue", this, [=] { controller_->removeQueue(element); })->setObjectName("actionRemoveQueue");
    }
    menu->popup(at);
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

void Flows::cutLink(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
    controller_->cut(element, identity);
}

void Flows::disconnectLinkOnly(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
    controller_->disconnectLink(element, identity);
}

void Flows::addQueue(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
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
    for (const gps::Connection& l : controller_->plan().links) {
        std::string templateName;
        int k = 0;
        if (l.srcElement == e->name && gps::splitPadIdentity(l.srcPad, &templateName, &k)) ++in.linkedSrc[templateName];
        if (l.sinkElement == e->name) ++in.linkedSink[l.sinkPad];
    }
    auto* page = new PropertyPage(in);
    connect(page, &PropertyPage::confirmed, this, [this, page, element] {
        if (controller_->editElement(element, page->edit())) closeProperties();
    });
    connect(page, &PropertyPage::deleteRequested, this, [this, element] {
        closeProperties();
        deleteElementOnly(element);
    });
    presentProperties(page, "Edit properties · " + title(element));
}

void Flows::deleteSource(const QString& element) {
    if (controller_->editingBlocked()) return;
    controller_->deleteSource(element);
}

void Flows::deleteElementOnly(const QString& element) {
    if (controller_->editingBlocked()) return;
    controller_->deleteElement(element, true);
}
