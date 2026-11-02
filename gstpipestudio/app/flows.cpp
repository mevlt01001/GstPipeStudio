// flows.cpp
#include "flows.h"

#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <memory>

#include "capsdialog.h"
#include "confirmdialog.h"
#include "pickerdialog.h"
#include "propertydialog.h"

namespace {

QString fromStd(const std::string& s) { return QString::fromStdString(s); }

QString pathText(const QString& element, const QString& identity) { return element + "." + identity; }

}  // namespace

Flows::Flows(Controller* controller, QWidget* window) : QObject(window), controller_(controller), window_(window) {}

void Flows::present(QDialog* dialog, std::function<void()> accepted) {
    connect(dialog, &QDialog::accepted, this, std::move(accepted));
    connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
    dialog->open();
}

void Flows::confirmRemoval(const gps::Removal& removal, const QString& confirmText, std::function<void()> accepted) {
    if (removal.elements.empty()) {
        accepted();
        return;
    }
    QStringList lines;
    for (const std::string& name : removal.elements) {
        const gps::PlanElement* e = controller_->plan().find(name);
        lines << fromStd(name) + (e ? "   (" + fromStd(e->factory) + ")" : QString());
    }
    QString heading = QString("Şu %1 element silinecek (%2 bağlantıyla birlikte)").arg(removal.elements.size()).arg(removal.links);
    present(new ConfirmDialog("Silinecekler", heading, lines, confirmText, window_), std::move(accepted));
}

// ---- Kaynak ekle

void Flows::addSource(const QString& factory) {
    if (controller_->busy()) return;
    controller_->fetchInfo(factory, [this, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyDialogInput in;
        in.info = *info;
        in.title = "Kaynak ekle: " + factory;
        std::vector<std::pair<std::string, std::string>> srcTemplates;
        for (const PadTemplateInfo& t : info->srcTemplates()) srcTemplates.emplace_back(t.name.toStdString(), t.presence.toStdString());
        in.requestSrc = gps::defaultRequestSrc(srcTemplates);
        auto* dialog = new PropertyDialog(in, window_);
        present(dialog, [this, dialog, factory] { controller_->addSource(factory, dialog->edit()); });
    });
}

void Flows::chooseSource() {
    if (controller_->busy()) return;
    PickerData data;
    data.title = "Kaynak ekle";
    data.subtitle = "Pipeline'ın başlangıç elementini seç.";
    data.newHeader = "Kaynaklar";
    for (const ListEntry& e : controller_->catalog()) {
        if (e.isSource) data.candidates << e;
    }
    auto* dialog = new PickerDialog(data, window_);
    present(dialog, [this, dialog] { addSource(dialog->choice().factory); });
}

// ---- left click on a free src pad

void Flows::fromFreePad(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    QString allowed = controller_->padAllowed(element, identity);
    controller_->fetchCompatible(allowed, [this, element, identity, allowed](const QStringList& compatible) {
        PickerData data;
        data.title = "Seçici";
        data.subtitle = pathText(element, identity) + (allowed.isEmpty() ? QString() : "   ·   " + mediaOf(allowed));
        data.existing = controller_->existingTargets(element, identity);
        data.compatible = compatible;
        for (const ListEntry& e : controller_->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        auto* dialog = new PickerDialog(data, window_);
        present(dialog, [this, dialog, element, identity] {
            PickerChoice choice = dialog->choice();
            if (choice.kind == PickerChoice::Existing) {
                editThenCaps(element, identity, [=] {
                    return controller_->linkExisting(element, identity, choice.existing.element, choice.existing.sinkPad);
                });
            } else {
                newElement(Mode::Add, element, identity, choice.factory);
            }
        });
    });
}

void Flows::newElement(Mode mode, const QString& element, const QString& identity, const QString& factory) {
    controller_->fetchInfo(factory, [this, mode, element, identity, factory](const ElementInfo* info) {
        if (!info) return;
        PropertyDialogInput in;
        in.info = *info;
        in.title = (mode == Mode::Add ? "Element ekle: " : "Element değiştir: ") + factory;
        std::vector<std::pair<std::string, std::string>> srcTemplates;
        for (const PadTemplateInfo& t : info->srcTemplates()) srcTemplates.emplace_back(t.name.toStdString(), t.presence.toStdString());
        in.requestSrc = gps::defaultRequestSrc(srcTemplates);
        auto* dialog = new PropertyDialog(in, window_);
        present(dialog, [this, mode, dialog, info, element, identity, factory] {
            ElementEdit edit = dialog->edit();
            chooseSinkPad(controller_->sinkChoices(*info, element, identity), [=](const QString& sinkPad) {
                auto apply = [=] {
                    editThenCaps(element, identity, [=] {
                        return mode == Mode::Add ? controller_->addAfter(element, identity, factory, sinkPad, edit)
                                                 : controller_->replace(element, identity, factory, sinkPad, edit);
                    });
                };
                if (mode == Mode::Replace) confirmRemoval(controller_->previewReplace(element, identity), "Değiştir", apply);
                else apply();
            });
        });
    });
}

void Flows::chooseSinkPad(const QList<PadTemplateInfo>& choices, std::function<void(const QString&)> chosen) {
    if (choices.size() == 1) {
        chosen(choices.first().name);
        return;
    }
    if (choices.isEmpty()) return;

    QStringList names;
    for (const PadTemplateInfo& t : choices) names << t.name;
    auto* dialog = new QInputDialog(window_);
    dialog->setObjectName("sinkPadDialog");
    dialog->setWindowTitle("Sink pad");
    dialog->setLabelText("Hangi sink pad'e bağlansın?");
    dialog->setComboBoxItems(names);
    dialog->setOkButtonText("Seç");
    dialog->setCancelButtonText("İptal");
    present(dialog, [dialog, chosen] { chosen(dialog->textValue()); });
}

void Flows::editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(controller_, &Controller::updateFinished, this, [this, connection, element, identity](bool ok) {
        disconnect(*connection);
        if (ok) openCaps(element, identity, false);
    });
    if (!edit()) disconnect(*connection);
}

void Flows::openCaps(const QString& element, const QString& identity, bool always) {
    int link = controller_->plan().linkFrom(element.toStdString(), identity.toStdString());
    if (link < 0) return;

    CapsDialogInput in;
    in.title = "Caps düzenle";
    const gps::PlanLink& l = controller_->plan().links[link];
    in.subtitle = pathText(element, identity) + "  →  " + pathText(fromStd(l.sinkElement), fromStd(l.sinkPad));
    in.allowed = controller_->padStructures(element, identity);
    in.current = fromStd(l.caps);
    if (const PadTemplateInfo* sink = controller_->linkSinkTemplate(link)) {
        for (const PropertyInfo& p : sink->padProperties) {
            if (PropertyEditor::editable(p)) in.padProperties << p;
        }
    }
    in.padProps = l.sinkPadProps;
    if (!always && in.allowed.isEmpty() && in.padProperties.isEmpty()) return;  // nothing to choose

    auto* dialog = new CapsDialog(in, window_);
    present(dialog, [this, dialog, element, identity] {
        controller_->setCaps(element, identity, dialog->caps(), dialog->padProps());
    });
}

// ---- menus

void Flows::showLinkMenu(const QString& element, const QString& identity, const QPoint& at) {
    if (controller_->busy()) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("linkMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Caps düzenle", this, [=] { editCaps(element, identity); })->setObjectName("actionEditCaps");
    menu->addAction("Element değiştir", this, [=] { replaceBehind(element, identity); })->setObjectName("actionReplace");
    menu->addAction("Bağlantıyı kaldır", this, [=] { cutLink(element, identity); })->setObjectName("actionCut");
    menu->addAction("Queue ekle", this, [=] { addQueue(element, identity); })->setObjectName("actionInsertQueue");
    menu->popup(at);
}

void Flows::showElementMenu(const QString& element, const QPoint& at) {
    if (controller_->busy()) return;
    const gps::PlanElement* e = controller_->plan().find(element.toStdString());
    if (!e) return;
    auto* menu = new QMenu(window_);
    menu->setObjectName("elementMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Property düzenle", this, [=] { editProperties(element); })->setObjectName("actionProperties");
    if (controller_->view(element).isSource) menu->addAction("Sil", this, [=] { deleteSource(element); })->setObjectName("actionDelete");
    if (e->factory == gps::kQueueFactory) {
        menu->addAction("Queue kaldır", this, [=] { controller_->removeQueue(element); })->setObjectName("actionRemoveQueue");
    }
    menu->popup(at);
}

// ---- link and element actions

void Flows::editCaps(const QString& element, const QString& identity) {
    if (!controller_->busy()) openCaps(element, identity, true);
}

void Flows::replaceBehind(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    QString allowed = controller_->padAllowed(element, identity);
    controller_->fetchCompatible(allowed, [this, element, identity, allowed](const QStringList& compatible) {
        PickerData data;
        data.title = "Element değiştir";
        data.subtitle = pathText(element, identity) + " arkasındaki element yerine";
        data.compatible = compatible;
        for (const ListEntry& e : controller_->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        auto* dialog = new PickerDialog(data, window_);
        present(dialog, [this, dialog, element, identity] { newElement(Mode::Replace, element, identity, dialog->choice().factory); });
    });
}

void Flows::cutLink(const QString& element, const QString& identity) {
    if (controller_->busy()) return;
    confirmRemoval(controller_->previewCut(element, identity), "Kaldır", [this, element, identity] { controller_->cut(element, identity); });
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

    PropertyDialogInput in;
    in.info = *info;
    in.title = "Property düzenle: " + element;
    in.props = e->props;
    in.requestSrc = e->requestSrc;
    for (const gps::PlanLink& l : controller_->plan().links) {
        std::string templateName;
        int k = 0;
        if (l.srcElement == e->name && gps::splitPadIdentity(l.srcPad, &templateName, &k)) ++in.linkedSrc[templateName];
    }
    for (const gps::ExposedParam& p : controller_->plan().exposed) {
        if (p.element == e->name) in.exposed.push_back(p.property);
    }
    auto* dialog = new PropertyDialog(in, window_);
    present(dialog, [this, dialog, element] { controller_->editElement(element, dialog->edit()); });
}

void Flows::deleteSource(const QString& element) {
    if (controller_->busy()) return;
    confirmRemoval(controller_->previewDeleteSource(element), "Sil", [this, element] { controller_->deleteSource(element); });
}
