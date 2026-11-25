// saveflow.h – the one place that saves for the user, says why a save failed, and asks about unsaved changes.
// Save of an untitled document asks for a project first (New project); Save as binds the tab to the file chosen;
// closing asks Save / Don't save / Cancel once: about one tab, or about every unsaved one of several in one list.
#pragma once

#include <QList>
#include <QObject>
#include <memory>

class Document;
class MainWindow;
class SaveTarget;
struct SaveResult;

class SaveFlow : public QObject {
    Q_OBJECT
public:
    explicit SaveFlow(MainWindow* window);

    bool save(Document* document);
    bool saveAs(Document* document, const QString& path = QString());  // "" asks for the file
    bool saveAll(const QList<Document*>& documents);  // the unsaved ones, in order; false at the first cancel or failure
    // True when the documents may go: their changes saved or dropped, or nothing to ask about; false on Cancel.
    bool confirmClose(const QList<Document*>& documents);

private:
    enum class Answer { Save, Discard, Cancel };
    Answer ask(const QList<Document*>& unsaved) const;
    bool report(Document* document, const SaveResult& result) const;  // false, said in the status bar and the Log

    MainWindow* window_;
};
