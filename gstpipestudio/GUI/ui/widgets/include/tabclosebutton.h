// tabclosebutton.h – a document tab's close button, as editors draw it: "●" while the document has unsaved changes,
// "×" otherwise and whenever the pointer is on it, so the dot shows at a glance and the button still closes.
#pragma once

#include <QToolButton>

class TabCloseButton : public QToolButton {
    Q_OBJECT
public:
    explicit TabCloseButton(QWidget* parent = nullptr);
    void setDirty(bool dirty);
    bool dirty() const { return dirty_; }

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void refresh();
    bool dirty_ = false;
};
