// templateinstances.h – the template copies on one document's canvas, like modules a file imports from the library:
// which are expanded, which are being edited (their changes go to the library as the template's next revision), which
// differ from their template (the window asks what that means, once) and which the library has newer revisions of
// (offered once per revision). Making a template, its parameters, renames in the library and updates come here too.
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <map>
#include <string>
#include <vector>

#include "graph.h"

class DocumentViews;
class EditSession;
class FactoryCatalog;
class TemplateLibrary;
namespace gps {
class Pipeline;
}

class TemplateInstances : public QObject {
    Q_OBJECT
public:
    TemplateInstances(EditSession& edits, const gps::Pipeline& pipeline, TemplateLibrary& library, const FactoryCatalog& catalog,
                      const DocumentViews& views, QObject* parent = nullptr);

    // ---- expanded copies: their members are drawn inside a frame
    bool expanded(const QString& id) const { return expanded_.contains(id); }
    QSet<QString> expandedSet() const { return expanded_; }
    void setExpanded(const QString& id, bool expanded);  // only the mark: the canvas makes room (Controller)

    // ---- making a template: the selection becomes a copy at once; finish names it, gives it its parameters and writes
    // it to the library; cancel takes it back.
    QString begin(const QStringList& elements);  // the new copy, "" when refused
    bool finish(const QString& id, const QString& name, const std::vector<gps::ExposedParam>& params, QString* error);
    bool detach(const QString& id);  // its elements stay, plain
    std::vector<gps::ExposedParam> parameterChoices(const QStringList& elements) const;  // what Add parameter offers

    // ---- a copy whose content changed (templateChanged): keep editing it, or make it a template of its own
    void keepEditing(const QString& id);
    bool saveAsNew(const QString& id, const QString& name, QString* error);
    bool commit(const QString& id);  // a copy being edited becomes the template's next revision; unchanged copies follow
    void commitAll();                // every edited copy (saving the file ends their editing)

    QString libraryOf(const QString& id) const;  // the library template a copy is of, "" when the copy is gone

    // ---- the library's newer revisions
    QStringList outdated() const;           // copies whose library template is newer
    bool update(const QStringList& ids);    // they take the library's content: one undo step; updated() follows
    void offer();                           // templatesOutdated soon, for revisions not offered yet

    // ---- parameters: of a placed copy (its collapsed box shows these), and of a library template being edited
    bool setParams(const QString& id, const std::map<std::string, std::string>& values);  // name -> value, "" = default
    void setTemplateParameters(const std::vector<gps::ExposedParam>& params);

    // Which copies differ from their template; ask: the first time one does, templateChanged (unless it is being
    // edited). Undo and opening a file only take note.
    void check(bool ask);
    void forget();  // a new plan: nothing is being edited or was offered

signals:
    void templateChanged(const QString& id);         // its content no longer matches its template: what now?
    void templatesOutdated(const QStringList& ids);  // the library holds newer versions of copies on this canvas
    void updated();                                  // update() took newer content: the canvas is aligned again
    void notice(const QString& text);
    void logLine(const QString& line);

private:
    const gps::Graph& plan() const;
    bool onCanvas(const QString& id, QString* error) const;  // false, with the reason, when it is gone
    bool write(const QString& name, const QString& id, QString* error);  // the copy to the library as template name
    void followRename(const QString& from, const QString& to);  // the library renamed a template: its copies say so
    void offerNow();

    EditSession& edits_;
    const gps::Pipeline& pipeline_;
    TemplateLibrary& library_;
    const FactoryCatalog& catalog_;
    const DocumentViews& views_;
    QSet<QString> expanded_;
    QSet<QString> editing_;        // copies whose changes go to their template (Update template was chosen)
    QSet<QString> knownModified_;  // copies already known to differ from their template
    QTimer offerTimer_;            // coalesces the offers of one burst of library changes
    QHash<QString, int> offered_;  // copy id -> the library revision it was last offered
};
