// flows.cpp
#include "flows.h"

#include <QAction>
#include <QMenu>

#include "capspage.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "pickerpage.h"
#include "propertypage.h"

namespace {

QString pathText(const QString& element, const QString& identity) { return element + "." + identity; }

QString pluralElements(int n) { return QString("%1 element%2").arg(n).arg(n == 1 ? "" : "s"); }

}  // namespace

Flows::Flows(Controller* controller, Drawer* drawer, QWidget* window)
    : QObject(window), controller_(controller), drawer_(drawer), window_(window) {
    connect(drawer_, &Drawer::closed, this, &Flows::abandonPending);
}

// The user closed the drawer while the validation of an edit was still running: no caps page opens afterwards.
void Flows::abandonPending() { disconnect(pending_); }

void Flows::confirmRemoval(const gps::Removal& removal, const QString& verb, std::function<void()> accepted) {
    if (removal.elements.empty()) {
        accepted();
        return;
    }
    QStringList lines;
    for (const std::string& name : removal.elements) {
        const gps::PlanElement* e = controller_->plan().find(name);
        lines << fromStd(name) + (e ? "   (" + fromStd(e->factory) + ")" : QString());
    }
    int n = static_cast<int>(removal.elements.size());
    QString heading = QString("These %1 will be removed, with %2 link%3").arg(pluralElements(n)).arg(removal.links).arg(removal.links == 1 ? "" : "s");
    auto* dialog = new ConfirmDialog("Remove elements", heading, lines, verb + " " + pluralElements(n), window_);
    connect(dialog, &QDialog::accepted, this, std::move(accepted));
    connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
    dialog->open();
}

// ---- Add source

void Flows::chooseSource() {
    if (controller_->busy()) return;
    PickerData data;
    data.newHeader = "Sources";
    for (const ListEntry& e : controller_->catalog()) {
        if (e.isSource) data.candidates << e;
    }
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) { sourceProperties(choice.factory); });
    drawer_->present(page, "Add source", "Choose the first element of the pipeline.", Drawer::Replace);
}

void Flows::chooseElement() {
    if (controller_->busy()) return;
    PickerData data;
    data.newHeader = "Elements";
    data.showFilters = true;
    data.candidates = controller_->catalog();
    auto* page = new PickerPage(data);
    connect(page, &PickerPage::picked, this, [this](const PickerChoice& choice) { sourceProperties(choice.factory); });
    drawer_->present(page, "Add element", "Choose any element (SRC, FILTER, or SINK) to place on the canvas.", Drawer::Replace);
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
            if (controller_->addElement(factory, page->edit())) drawer_->dismiss();
        });
        QString prefix = info->isSource ? "Add source · " : "Add element · ";
        drawer_->present(page, prefix + factory, QString(), Drawer::Push);
    });
}

// ---- left click on a free src pad

void Flows::fromFreePad(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    QString allowed = controller_->padAllowed(element, identity);
    controller_->fetchCompatible(allowed, [this, element, identity, allowed](const QStringList& compatible) {
        PickerData data;
        data.existing = controller_->existingTargets(element, identity);
        data.compatible = compatible;
        for (const ListEntry& e : controller_->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        auto* page = new PickerPage(data);
        connect(page, &PickerPage::picked, this, [this, element, identity](const PickerChoice& choice) {
            if (choice.kind == PickerChoice::Existing) {
                editThenCaps(element, identity, [=] {
                    return controller_->linkExisting(element, identity, choice.existing.element, choice.existing.sinkPad);
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
            auto apply = [=] {
                editThenCaps(element, identity, [=] {
                    return mode == Mode::Add ? controller_->addAfter(element, identity, factory, sinkPad, edit)
                                             : controller_->replace(element, identity, factory, sinkPad, edit);
                });
            };
            if (mode == Mode::Replace) confirmRemoval(controller_->previewReplace(element, identity), "Replace", apply);
            else apply();
        });
        QString title = (mode == Mode::Add ? "Add after " : "Replace after ") + pathText(element, identity) + " · " + factory;
        drawer_->present(page, title, QString(), Drawer::Push);
    });
}

void Flows::editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(controller_, &Controller::updateFinished, this, [this, connection, element, identity](bool ok) {
        disconnect(*connection);
        if (ok) openCaps(element, identity, true);
        else drawer_->dismiss();
    });
    pending_ = *connection;
    if (!edit()) disconnect(*connection);  // refused: the page stays as it is
}

void Flows::openCaps(const QString& element, const QString& identity, bool afterEdit) {
    int link = controller_->plan().linkFrom(element.toStdString(), identity.toStdString());
    if (link < 0) {
        drawer_->dismiss();
        return;
    }
    CapsPageInput in;
    const gps::PlanLink& l = controller_->plan().links[link];
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
        drawer_->dismiss();
        return;
    }

    auto* page = new CapsPage(in);
    connect(page, &CapsPage::applied, this, [this, page, element, identity] {
        controller_->setCaps(element, identity, page->caps(), page->padProps());
        drawer_->dismiss();
    });
    connect(page, &CapsPage::skipped, drawer_, &Drawer::dismiss);
    QString title = (afterEdit ? "Caps · " : "Edit caps · ") + pathText(element, identity);
    QString sub = pathText(element, identity) + "  →  " + pathText(fromStd(l.sinkElement), fromStd(l.sinkPad));
    drawer_->present(page, title, sub, Drawer::Replace);
}

// ---- menus

void Flows::showLinkMenu(const QString& element, const QString& identity, const QPoint& at) {
    if (controller_->busy()) return;
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
    if (controller_->busy()) return;
    const gps::PlanElement* e = controller_->plan().find(element.toStdString());
    if (!e) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("elementMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Edit properties…", this, [=] { editProperties(element); })->setObjectName("actionProperties");
    if (controller_->view(element).isSource) menu->addAction("Delete source", this, [=] { deleteSource(element); })->setObjectName("actionDelete");
    if (e->factory == gps::kQueueFactory) {
        menu->addAction("Remove queue", this, [=] { controller_->removeQueue(element); })->setObjectName("actionRemoveQueue");
    }
    menu->popup(at);
}

// ---- link and element actions

void Flows::editCaps(const QString& element, const QString& identity) {
    if (!controller_->busy()) openCaps(element, identity, false);
}

void Flows::replaceBehind(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
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
    if (controller_->busy()) return;
    confirmRemoval(controller_->previewCut(element, identity), "Remove", [this, element, identity] { controller_->cut(element, identity); });
}

void Flows::disconnectLinkOnly(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    controller_->disconnectLink(element, identity);
}

void Flows::addQueue(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    controller_->fetchInfo(gps::kQueueFactory, [this, element, identity](const ElementInfo* info) {
        if (info) controller_->insertQueue(element, identity);
    });
}

void Flows::editProperties(const QString& element) {
    if (controller_->busy()) return;
    const gps::PlanElement* e = controller_->plan().find(element.toStdString());
    const ElementInfo* info = e ? controller_->info(fromStd(e->factory)) : nullptr;
    if (!info) return;

    PropertyPageInput in;
    in.info = *info;
    in.acceptText = "Save";
    in.props = e->props;
    in.requestSrc = e->requestSrc;
    in.requestSink = e->requestSink;
    for (const gps::PlanLink& l : controller_->plan().links) {
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
        if (controller_->editElement(element, page->edit())) drawer_->dismiss();
    });
    drawer_->present(page, "Edit properties · " + element, QString(), Drawer::Replace);
}

void Flows::deleteSource(const QString& element) {
    if (controller_->busy()) return;
    confirmRemoval(controller_->previewDeleteSource(element), "Delete", [this, element] { controller_->deleteSource(element); });
}

void Flows::deleteElementOnly(const QString& element) {
    if (controller_->busy()) return;
    controller_->deleteElement(element, true);
}
