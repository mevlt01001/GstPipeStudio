// contextmenus.cpp – see contextmenus.h.
#include "contextmenus.h"

#include <QMenu>

#include "canvasscene.h"
#include "controller.h"
#include "document.h"

namespace {
constexpr std::nullopt_t separator = std::nullopt;
}

ContextMenus::ContextMenus(CommandSet& commands, QWidget* window) : QObject(window), commands_(commands), window_(window) {}

void ContextMenus::canvas(Document& d, const QPoint& at, const QPointF& scenePos) {
    commands_.setTarget({QString(), QString(), QString(), scenePos});
    commands_.refresh();
    auto* menu = new QMenu(window_);
    menu->setObjectName("canvasMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* addMenu = menu->addMenu("&Add");
    addMenu->setObjectName("menuAdd");
    addMenu->addAction(commands_[Command::AddElement]);
    addMenu->addAction(commands_[Command::AddTemplate]);
    addMenu->addAction(commands_[Command::AddPlugin]);
    addMenu->addAction(commands_[Command::AddLaunch]);
    QList<Item> items;
    if (!d.selection().isEmpty())
        items << separator << Command::SaveAsTemplate << Command::AlignSelection << Command::ConnectSelected << Command::Duplicate
              << Command::Copy << Command::Cut << Command::Delete;
    items << separator << Command::Undo << Command::Redo << Command::Paste << separator << Command::SelectAll << Command::Fit
          << Command::Arrange << Command::Validate;
    for (const Item& item : items) {
        if (item) menu->addAction(commands_[*item]);
        else menu->addSeparator();
    }
    menu->popup(at);
}

void ContextMenus::element(Document& d, const QString& element, const QPoint& at) {
    const Outline outline = d.controller()->outline();
    if (!d.selection().contains(element)) d.scene()->setSelectedElements(outline.expand({element}));
    if (d.selection().size() > 1 || !outline.collapsedTemplate(element).isEmpty()) {
        selection(d, element, at);
        return;
    }
    QList<Item> items{Command::EditProperties, Command::Copy, Command::Duplicate, Command::Paste, Command::Delete};
    const gps::Element* e = d.controller()->plan().find(toStd(element));
    if (e && e->factory == gps::kQueueFactory) items << Command::RemoveQueue;
    popup("elementMenu", {element, QString(), QString()}, items, at);
}

// Inside a collapsed template, the template's own commands come first.
void ContextMenus::selection(Document& d, const QString& clicked, const QPoint& at) {
    const QString copy = d.controller()->outline().collapsedTemplate(clicked);
    QList<Item> items;
    if (!copy.isEmpty()) items << Command::EditTemplate << Command::ToggleTemplate << Command::DetachTemplate << separator;
    items << Command::SaveAsTemplate << separator << Command::ConnectSelected << Command::DisconnectSelected << Command::AlignSelection
          << separator << Command::Duplicate << Command::Copy << Command::Cut << Command::Delete;
    popup("selectionMenu", {clicked, QString(), copy}, items, at);
}

void ContextMenus::templateFrame(Document& d, const QString& instanceId, const QPoint& at) {
    if (!d.controller()->plan().instance(toStd(instanceId))) return;
    popup("templateMenu", {QString(), QString(), instanceId},
          {Command::EditTemplate, separator, Command::ToggleTemplate, Command::AlignSelection, Command::SaveAsTemplate,
           Command::DetachTemplate, separator, Command::Delete},
          at);
}

void ContextMenus::link(const QString& element, const QString& pad, const QPoint& at) {
    popup("linkMenu", {element, pad, QString()}, {Command::EditCaps, Command::ReplaceBehind, Command::CutLink, Command::InsertQueue}, at);
}

void ContextMenus::tab(const QPoint& at) {
    popup("tabMenu", {}, {Command::Save, Command::SaveAs, Command::Revert, Command::VersionHistory, separator, Command::CloseTab}, at);
}

// The commands are refreshed for the target first: their words and rules say what they would do to it.
void ContextMenus::popup(const char* name, const CommandSet::Target& target, const QList<Item>& items, const QPoint& at) {
    commands_.setTarget(target);
    commands_.refresh();
    auto* menu = new QMenu(window_);
    menu->setObjectName(name);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const Item& item : items) {
        if (item) menu->addAction(commands_[*item]);
        else menu->addSeparator();
    }
    menu->popup(at);
}
