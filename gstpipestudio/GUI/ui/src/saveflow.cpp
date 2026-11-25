// saveflow.cpp – see saveflow.h.
#include "saveflow.h"

#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>

#include "controller.h"
#include "document.h"
#include "logpanel.h"
#include "mainwindow.h"
#include "newprojectdialog.h"
#include "pipelinefiledialog.h"
#include "savetarget.h"
#include "settings.h"

SaveFlow::SaveFlow(MainWindow* window) : QObject(window), window_(window) {}

bool SaveFlow::save(Document* d) {
    Controller* c = d->controller();
    if (!c->target().needsDestination()) return report(d, c->save());
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
    QMessageBox box(window_);
    box.setObjectName("saveChanges");
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Unsaved changes");
    box.setText(one ? "Do you want to save the changes you made to " + titles.first() + "?"
                    : QString("Do you want to save the changes to the following %1 files?").arg(unsaved.size()));
    box.setInformativeText((one ? QString() : titles.join("\n") + "\n\n") + "Your changes will be lost if you don't save them.");
    QPushButton* saveButton = box.addButton(one ? "Save" : "Save all", QMessageBox::AcceptRole);
    QPushButton* dropButton = box.addButton("Don't save", QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    saveButton->setObjectName("saveChangesSave");
    dropButton->setObjectName("saveChangesDiscard");
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == saveButton) return Answer::Save;
    return box.clickedButton() == dropButton ? Answer::Discard : Answer::Cancel;
}

bool SaveFlow::report(Document* d, const SaveResult& r) const {
    if (r.ok) return true;
    window_->statusBar()->showMessage(r.error, 10000);
    d->log()->append(r.error);
    return false;
}
