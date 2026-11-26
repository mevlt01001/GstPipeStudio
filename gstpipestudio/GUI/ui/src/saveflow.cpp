// saveflow.cpp – see saveflow.h.
#include "saveflow.h"

#include <QStatusBar>

#include "controller.h"
#include "document.h"
#include "documentregistry.h"
#include "logpanel.h"
#include "mainwindow.h"
#include "newprojectdialog.h"
#include "pipelinefiledialog.h"
#include "prompt.h"
#include "savetarget.h"
#include "settings.h"

SaveFlow::SaveFlow(MainWindow* window) : QObject(window), window_(window) {}

bool SaveFlow::save(Document* d) {
    Controller* c = d->controller();
    if (!c->target().needsDestination()) return overwrite(d) && report(d, c->save());
    window_->activate(d);  // the project asked for is this tab's
    const QString conf = NewProjectDialog::ask(window_);
    return !conf.isEmpty() && report(d, c->saveAs(std::make_unique<ProjectConfTarget>(conf)));
}

bool SaveFlow::saveAs(Document* d, const QString& path) {
    QString file = path;
    if (file.isEmpty()) file = PipelineFileDialog::choose(window_, true, d->path().isEmpty() ? Settings::lastDir() : d->path());
    if (file.isEmpty()) return false;
    if (!file.endsWith(".conf")) file += ".conf";
    return report(d, d->controller()->saveAs(std::make_unique<ProjectConfTarget>(file)));
}

bool SaveFlow::saveAll(const QList<Document*>& documents) {
    for (Document* d : documents) {
        if (d->controller()->hasUnsavedWork() && !save(d)) return false;
    }
    return true;
}

bool SaveFlow::confirmClose(const QList<Document*>& documents) {
    QList<Document*> unsaved;
    for (Document* d : documents) {
        if (d->controller()->hasUnsavedWork()) unsaved << d;
    }
    if (unsaved.isEmpty()) return true;
    if (unsaved.size() == 1) window_->activate(unsaved.first());
    switch (ask(unsaved)) {
    case Answer::Save: return saveAll(unsaved);
    case Answer::Discard: return true;
    case Answer::Cancel: break;
    }
    return false;
}

// As editors ask: about the one tab by name, or about several in one list.
SaveFlow::Answer SaveFlow::ask(const QList<Document*>& unsaved) const {
    const bool one = unsaved.size() == 1;
    QStringList titles;
    for (const Document* d : unsaved) titles << d->title();
    const int answer = Prompt::ask(window_, {"saveChanges", "Unsaved changes",
        one ? "Do you want to save the changes you made to " + titles.first() + "?"
            : QString("Do you want to save the changes to the following %1 files?").arg(unsaved.size()),
        (one ? QString() : titles.join("\n") + "\n\n") + "Your changes will be lost if you don't save them.",
        {{one ? "Save" : "Save all", "saveChangesSave"}, {"Don't save", "saveChangesDiscard", QMessageBox::DestructiveRole}, Prompt::cancel()},
        0, QMessageBox::Warning});
    return answer == 0 ? Answer::Save : answer == 1 ? Answer::Discard : Answer::Cancel;
}

bool SaveFlow::overwrite(Document* d) const {
    if (!window_->registry()->changedSinceRead(d)) return true;
    window_->activate(d);
    return Prompt::ask(window_, {"changedOnDiskSave", "File changed on disk", d->title() + " was changed on disk by another program.",
                                 "Overwrite it with your version?", {{"Overwrite", "overwriteOnDisk"}, Prompt::cancel()}, 0,
                                 QMessageBox::Warning}) == 0;
}

bool SaveFlow::report(Document* d, const SaveResult& r) const {
    if (r.ok) return true;
    window_->statusBar()->showMessage(r.error, 10000);
    d->log()->append(r.error);
    return false;
}
