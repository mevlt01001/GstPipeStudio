// contextmenus.h – the right-click menus of the canvas (empty space, an element, a selection, a template frame, a link)
// and of a tab, each assembled from the window's commands (CommandSet): the same actions, words and rules as the menu
// bar. A menu about one element, link or template copy sets the commands' target first.
#pragma once

#include <QList>
#include <QObject>
#include <optional>

#include "commandset.h"

class Document;
class QPoint;
class QWidget;

class ContextMenus : public QObject {
    Q_OBJECT
public:
    ContextMenus(CommandSet& commands, QWidget* window);

    void canvas(Document& document, const QPoint& at);
    // An element: its own menu, or the selection's when several are selected or it is inside a collapsed template.
    // Not selected yet, it becomes the selection, as a right click makes it.
    void element(Document& document, const QString& element, const QPoint& at);
    void templateFrame(Document& document, const QString& instanceId, const QPoint& at);
    void link(const QString& element, const QString& pad, const QPoint& at);
    void tab(const QPoint& at);  // the tab is in front already

private:
    using Item = std::optional<Command>;  // nullopt: a separator
    void selection(Document& document, const QString& clicked, const QPoint& at);
    void popup(const char* name, const CommandSet::Target& target, const QList<Item>& items, const QPoint& at);

    CommandSet& commands_;
    QWidget* window_;
};
