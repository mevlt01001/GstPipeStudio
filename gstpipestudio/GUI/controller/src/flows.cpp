// flows.cpp
#include "flows.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QTabWidget>
#include <QVBoxLayout>

#include "capspage.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "theme.h"

namespace {

QString pathText(const QString& element, const QString& identity) { return element + "." + identity; }

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

void Flows::setBottomPanels(QTabWidget* tabs, QWidget* propsTab, QWidget* capsTab, QWidget* defaultTab) {
    tabs_ = tabs;
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

PropertyPage* Flows::propertyPage() const {
    return propsTab_ ? propsTab_->findChild<PropertyPage*>() : nullptr;
}

CapsPage* Flows::capsPage() const {
    return capsTab_ ? capsTab_->findChild<CapsPage*>() : nullptr;
}

void Flows::presentProperties(PropertyPage* page, const QString& title) {
    drawer_->dismiss();
    if (!propsTab_ || !tabs_) {
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
    tabs_->setCurrentWidget(propsTab_);
    page->focusFirst();
}

void Flows::closeProperties() {
    abandonPending();  // closing while a validation runs: no caps step afterwards
    propsTitle_.clear();
    if (!propsTab_) return;
    bool wasCurrent = tabs_ && tabs_->currentWidget() == propsTab_;
    clearTabLayout(propsTab_);
    auto* layout = static_cast<QVBoxLayout*>(propsTab_->layout());
    auto* hint = new QLabel("Click an element on the canvas to inspect or edit its Properties.", propsTab_);
    hint->setObjectName("muted");
    layout->addWidget(hint);
    layout->addStretch(1);
    if (wasCurrent && tabs_ && defaultTab_) tabs_->setCurrentWidget(defaultTab_);
}

void Flows::presentCaps(CapsPage* page, const QString& title, const QString& subtitle) {
    drawer_->dismiss();
    if (!capsTab_ || !tabs_) {
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
    tabs_->setCurrentWidget(capsTab_);
    page->focusFirst();
}

void Flows::closeCaps() {
    capsTitle_.clear();
    if (!capsTab_) return;
    bool wasCurrent = tabs_ && tabs_->currentWidget() == capsTab_;
    clearTabLayout(capsTab_);
    auto* layout = static_cast<QVBoxLayout*>(capsTab_->layout());
    auto* hint = new QLabel("Click a link on the canvas to inspect or edit its Caps.", capsTab_);
    hint->setObjectName("muted");
    layout->addWidget(hint);
    layout->addStretch(1);
    if (wasCurrent && tabs_ && defaultTab_) tabs_->setCurrentWidget(defaultTab_);
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

// ---- Add source / Add element (Right Drawer ONLY presents PickerPage)

void Flows::chooseSource() {
    if (controller_->editingBlocked()) return;
    PickerData data;
    data.newHeader = "Sources";
    for (const CustomElementInfo& c : controller_->customElements()) {
        if (c.isSource) data.custom << c;
    }
    for (const ListEntry& e : controller_->catalog()) {
        if (e.isSource) data.candidates << e;
    }
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) {
        if (choice.kind == PickerChoice::CustomElement) {
            controller_->addCustomElement(choice.factory);
            drawer_->dismiss();
        } else {
            sourceProperties(choice.factory);
        }
    });
    drawer_->present(page, "Add source", "Choose the first element of the pipeline.", Drawer::Replace);
}

void Flows::chooseElement() {
    if (controller_->editingBlocked()) return;
    PickerData data;
    data.newHeader = "Elements";
    data.showFilters = true;
    data.custom = controller_->customElements();
    data.candidates = controller_->catalog();
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) {
        if (choice.kind == PickerChoice::CustomElement) {
            controller_->addCustomElement(choice.factory);
            drawer_->dismiss();
        } else {
            sourceProperties(choice.factory);
        }
    });
    drawer_->present(page, "Add element", "Choose any element (SRC, FILTER, SINK, or Custom) to place on the canvas.", Drawer::Replace);
}

void Flows::sourceProperties(const QString& factory) {
    controller_->fetchInfo(factory, [this, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyPageInput in;
        in.info = *info;
        in.requestSrc = info->defaultRequestSrc();
        in.requestSink = info->defaultRequestSink();
        auto* page = new PropertyPage(in);
        connect(page, &PropertyPage::confirmed, this, [this, page, factory] {
            if (controller_->addElement(factory, page->edit())) closeProperties();
        });
        QString prefix = info->isSource ? "Add source · " : "Add element · ";
        presentProperties(page, prefix + factory);
    });
}

// ---- left click on a free src pad (Right Drawer ONLY presents PickerPage)

void Flows::fromFreePad(const QString& element, const QString& identity) {
    if (controller_->editingBlocked()) return;
    QString allowed = controller_->padAllowed(element, identity);
    controller_->fetchCompatible(allowed, [this, element, identity, allowed](const QStringList& compatible) {
        PickerData data;
        data.existing = controller_->existingTargets(element, identity);
        for (const CustomElementInfo& c : controller_->customElements()) {
            if (c.hasFreeSink) data.custom << c;
        }
        data.compatible = compatible;
        for (const ListEntry& e : controller_->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        auto* page = new PickerPage(data);
        connect(page, &PickerPage::picked, this, [this, element, identity](const PickerChoice& choice) {
            if (choice.kind == PickerChoice::Existing) {
                drawer_->dismiss();
                editThenCaps(element, identity, [=] {
                    return controller_->linkExisting(element, identity, choice.existing.element, choice.existing.sinkPad);
                });
            } else if (choice.kind == PickerChoice::CustomElement) {
                drawer_->dismiss();
                editThenCaps(element, identity, [=] {
                    return !controller_->addCustomElementAfter(element, identity, choice.factory).isEmpty();
                });
            } else {
                newElement(Mode::Add, element, identity, choice.factory);
            }
        });
        drawer_->present(page, "Add after " + element + " · " + identity, allowed.isEmpty() ? QString() : mediaOf(allowed), Drawer::Replace);
    });
}

void Flows::newElement(Mode mode, const QString& element, const QString& identity, const QString& factory) {
    controller_->fetchInfo(factory, [this, mode, element, identity, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyPageInput in;
        in.info = *info;
        in.acceptText = mode == Mode::Add ? "Add element" : "Replace";
        in.requestSrc = info->defaultRequestSrc();
        QList<PadTemplateInfo> choices = controller_->sinkChoices(*info, element, identity);
        if (choices.isEmpty()) return;
        for (const PadTemplateInfo& t : choices) in.sinkPads << t.name;
        auto* page = new PropertyPage(in);
        connect(page, &PropertyPage::confirmed, this, [this, mode, page, element, identity, factory, onlyPad = choices.first().name] {
            ElementEdit edit = page->edit();
            QString sinkPad = page->sinkPad().isEmpty() ? onlyPad : page->sinkPad();
            editThenCaps(element, identity, [=] {
                return mode == Mode::Add ? controller_->addAfter(element, identity, factory, sinkPad, edit)
                                         : controller_->replace(element, identity, factory, sinkPad, edit);
            });
        });
        QString title = (mode == Mode::Add ? "Add after " : "Replace after ") + pathText(element, identity) + " · " + factory;
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
            if (PropertyEditor::editable(p)) in.padProperties << p;
        }
    }
    in.padProps = l.sinkPadProps;
    if (afterEdit && in.allowed.isEmpty() && in.padProperties.isEmpty()) {  // nothing to choose
        return;
    }

    auto* page = new CapsPage(in);
    connect(page, &CapsPage::applied, this, [this, page, element, identity] {
        controller_->setCaps(element, identity, page->caps(), page->padProps());
        closeCaps();
    });
    QString title = (afterEdit ? "Caps · " : "Edit caps · ") + pathText(element, identity);
    QString sub = pathText(element, identity) + "  →  " + pathText(fromStd(l.sinkElement), fromStd(l.sinkPad));
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
    controller_->fetchCompatible(allowed, [this, element, identity](const QStringList& compatible) {
        PickerData data;
        data.compatible = compatible;
        for (const ListEntry& e : controller_->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        auto* page = new PickerPage(data);
        connect(page, &PickerPage::picked, this, [this, element, identity](const PickerChoice& choice) {
            newElement(Mode::Replace, element, identity, choice.factory);
        });
        drawer_->present(page, "Replace after " + pathText(element, identity), "Choose the element that takes the place of the one behind this pad.", Drawer::Replace);
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
    controller_->fetchInfo(gps::kQueueFactory, [this, element, identity](const ElementInfo* info) {
        if (info) controller_->insertQueue(element, identity);
    });
}

void Flows::editProperties(const QString& element) {
    if (controller_->editingBlocked()) return;
    QString grp = controller_->elementGroup(element);
    if (!grp.isEmpty() && !controller_->isGroupExpanded(grp)) {
        controller_->setGroupExpanded(grp, true);
        return;
    }
    const gps::Element* e = controller_->plan().find(element.toStdString());
    const ElementInfo* info = e ? controller_->info(fromStd(e->factory)) : nullptr;
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
    for (const gps::ExposedParam& p : controller_->plan().exposed) {
        if (p.element == e->name) in.exposed.push_back(p.property);
    }
    auto* page = new PropertyPage(in);
    connect(page, &PropertyPage::confirmed, this, [this, page, element] {
        if (controller_->editElement(element, page->edit())) closeProperties();
    });
    connect(page, &PropertyPage::deleteRequested, this, [this, element] {
        closeProperties();
        deleteElementOnly(element);
    });
    presentProperties(page, "Edit properties · " + element);
}

void Flows::deleteSource(const QString& element) {
    if (controller_->editingBlocked()) return;
    controller_->deleteSource(element);
}

void Flows::deleteElementOnly(const QString& element) {
    if (controller_->editingBlocked()) return;
    controller_->deleteElement(element, true);
}
