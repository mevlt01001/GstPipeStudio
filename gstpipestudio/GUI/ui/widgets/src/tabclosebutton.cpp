// tabclosebutton.cpp – see tabclosebutton.h.
#include "tabclosebutton.h"

TabCloseButton::TabCloseButton(QWidget* parent) : QToolButton(parent) {
    setObjectName("tabClose");
    setAutoRaise(true);
    setCursor(Qt::ArrowCursor);
    setFixedSize(20, 20);
    setFocusPolicy(Qt::NoFocus);
    refresh();
}

void TabCloseButton::setDirty(bool dirty) {
    dirty_ = dirty;
    refresh();
}

void TabCloseButton::enterEvent(QEnterEvent* event) {
    QToolButton::enterEvent(event);
    refresh();
}

void TabCloseButton::leaveEvent(QEvent* event) {
    QToolButton::leaveEvent(event);
    refresh();
}

void TabCloseButton::refresh() {
    setText(dirty_ && !underMouse() ? "●" : "×");
    setToolTip(dirty_ ? "Unsaved changes · Close (Ctrl+W)" : "Close (Ctrl+W)");
}
