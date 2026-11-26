// explorerlook.h – how an Explorer entry looks, chosen in this one place from its kind and state: its text, icon,
// font, colour and tooltip, and the words of the menu actions it offers. Open files are bold, unsaved ones carry a dot,
// a file deleted under its tab is struck through, an outdated one is drawn in the warning colour, a missing one muted.
#pragma once

#include <QString>

#include "explorerentry.h"
#include "explorersource.h"

class QTreeWidgetItem;

namespace ExplorerLook {

constexpr const char* kDot = " ●";  // after the name of a file with unsaved changes

void apply(QTreeWidgetItem* item, const ExplorerEntry& entry);
QString text(const ExplorerEntry& entry);
QString tooltip(const ExplorerEntry& entry);
QString actionText(ExplorerAction action, const ExplorerEntry& entry);  // "Rename", "Edit template", "Reveal in file manager"
QString actionName(ExplorerAction action);                              // its object name: "explorerRename"

}  // namespace ExplorerLook
