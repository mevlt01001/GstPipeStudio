// drawer.h – the left drawer (UI_RULES 1 and 2). It replaces the picker, property and caps dialogs: a header with
// Back and Close, a stack of pages, a 150 ms slide. Closed by default; the canvas keeps its scroll position.
#pragma once

#include <QWidget>
#include <vector>

class QLabel;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class QToolButton;

// One step of a flow. Esc and the Close button act like the page's Cancel; Enter presses primary().
class DrawerPage : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;

    QPushButton* primary() const { return primary_; }
    virtual void focusFirst() {}  // the control that gets focus when the page appears
    virtual void focusSearch() {}

signals:
    void cancelled();

protected:
    QPushButton* primary_ = nullptr;
};

class Drawer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int slideWidth READ width WRITE setFixedWidth)  // what the slide animation moves
public:
    static constexpr int kWidth = 400;
    static constexpr int kSlideMs = 150;
    enum Placement { Replace, Push };

    explicit Drawer(QWidget* parent = nullptr);

    bool isOpen() const { return open_; }
    int depth() const { return static_cast<int>(pages_.size()); }
    DrawerPage* page() const { return open_ && !pages_.empty() ? pages_.back().widget : nullptr; }  // none once closed
    template <class T>
    T* page() const { return qobject_cast<T*>(page()); }
    QString title() const;
    QString subtitle() const;

    // Replace: the page starts a flow (no Back). Push: it goes on top, Back returns to the page below.
    void present(DrawerPage* page, const QString& title, const QString& subtitle, Placement placement);
    void goBack();
    void dismiss();                 // closes the drawer and drops all pages
    void setBusy(bool busy);        // an update runs: the page cannot be used
    void focusSearch();

signals:
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Entry {
        DrawerPage* widget;
        QString title, subtitle;
    };

    void showTop();
    void clearPages();
    void slideTo(int target);

    QWidget* inner_;
    QToolButton* back_;
    QToolButton* close_;
    QLabel* title_;
    QLabel* subtitle_;
    QStackedWidget* stack_;
    QPropertyAnimation* slide_;
    std::vector<Entry> pages_;
    bool open_ = false;
};
