// documenttabs.h – the open documents as the window's tabs, as editors have them: the tab bar, the stack of their
// canvases, the list in tab order and the one in front. A target opens once: its tab comes to the front; else it opens
// in the front tab when that is pristine, or in a new one. The session (the tabs open when the window closed, with
// their hot-exit backups) is saved and restored here. The window wires each document as it is added and asks before
// one closes.
#pragma once

#include <QList>
#include <QObject>
#include <memory>

class AppServices;
class Controller;
class Document;
class QPoint;
class QStackedWidget;
class QTabBar;
class QWidget;
class SaveTarget;
struct Backup;

class DocumentTabs : public QObject {
    Q_OBJECT
public:
    DocumentTabs(AppServices& services, QWidget* window);
    ~DocumentTabs() override;  // the documents go first, while the docks and stacks that show their pages still stand

    QTabBar* bar() const { return bar_; }
    QStackedWidget* pages() const { return pages_; }
    Document* front() const { return front_; }
    QList<Document*> all() const;  // in tab order

    // A new tab, in front: over the caller's controller, or one of its own. An untitled one reads Untitled-N, the lowest
    // N no other tab has.
    Document* add(Controller* controller = nullptr);
    Document* target();                                 // where a file opens: the front tab when it is pristine, else a new one
    Document* find(const QString& sessionKey) const;    // the tab bound to that target; a pristine tab is a blank, not a find
    // The tab of a target, opened as above; with a backup (its id), the backup's content on it, modified.
    Document* open(std::unique_ptr<SaveTarget> wanted, const QString& backup = QString());
    void activate(Document* document);  // brings its tab to the front
    void close(Document* document);     // its tab goes, with its backup, run and monitor; the window always keeps one tab
    void step(int by);                  // the next (1) or previous (-1) tab, round the ends
    void refresh(Document* document);   // its tab's text, tooltip and unsaved dot

    // The tabs by their targets' session keys, with their backups, and the one in front; an untitled tab without
    // unsaved work is not kept. Restored, each comes back with its unsaved changes (hot exit).
    void saveSession() const;
    void restoreSession();

signals:
    void added(Document* document);      // to be wired, before it comes to the front
    void activated(Document* document);  // it is in front now
    void closeRequested(Document* document);  // its close button or a middle click: the window asks first
    void menuRequested(const QPoint& at);     // right click on a tab, which is in front by then

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // a middle click closes a tab

private:
    Document* at(int index) const;
    int indexOf(Document* document) const;
    Document* reopen(const Backup& tab);  // a tab of restoreSession; null when there is nothing to reopen

    AppServices& services_;
    QWidget* window_;
    QTabBar* bar_;
    QStackedWidget* pages_;
    QList<Document*> docs_;
    Document* front_ = nullptr;
};
