// saveflow.h – the one place that saves for the user, says why a save failed, and asks about unsaved changes.
// Save of an untitled document asks for a project first (New project); Save over a file that changed on disk since the
// document read it asks Overwrite / Cancel; Save as binds the tab to the file chosen; closing tabs asks Save / Don't
// save / Cancel once: about one tab, or about every unsaved one of several in one list.
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
    // The unsaved ones, in order; false at the first cancel or failure. One that cannot be saved now (it runs) is
    // passed over and named in the status bar, and the answer is false.
    bool saveAll(const QList<Document*>& documents);
    // True when the documents may go: their changes saved or dropped, or nothing to ask about; false on Cancel.
    bool confirmClose(const QList<Document*>& documents);

private:
    static constexpr int kStopWaitMs = 5000;  // the longest Save waits for a closing tab's run to stop
    enum class Answer { Save, Discard, Cancel };
    Answer ask(const QList<Document*>& unsaved) const;
    void stopForSave(Document* document) const;  // its run and monitor stop, and it waits until they have
    bool overwrite(Document* document) const;  // its file changed on disk: true when the user overwrites it
    bool report(Document* document, const SaveResult& result) const;  // false, said in the status bar and the Log

    MainWindow* window_;
};
