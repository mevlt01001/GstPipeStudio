#include "pluginstudio.h"

#include "controller.h"
#include "document.h"
#include "mainwindow.h"
#include "logpanel.h"
#include "plugineditorwindow.h"
#include "pluginlibrary.h"
#include "pluginscaffolder.h"
#include "pluginwizarddialog.h"
#include "prompt.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>

PluginStudio::PluginStudio(PluginLibrary& library, MainWindow* window)
    : QObject(window), library_(library), window_(window) {
    connect(&library_, &PluginLibrary::notice, this, [this](const QString& text) { fail("Plugin library", text); });
    connect(&library_, &PluginLibrary::buildOutputLine, this, [this](const QString& id, const QString& text) {
        if (auto* front = window_->front()) front->log()->append("[" + id + "] " + text);
    });
}

void PluginStudio::create() {
    PluginWizardDialog wizard(library_, std::nullopt, window_);
    if (wizard.exec() != QDialog::Accepted) return;
    const auto spec = wizard.spec();
    QString error;
    if (!library_.createPlugin(spec, &error)) return fail("Unable to create plugin", error);
    if (wizard.opensEditor()) edit(spec.id);
    build(spec.id);
}

PluginEditorWindow* PluginStudio::edit(const QString& id) {
    if (library_.sourcePath(id).isEmpty()) return nullptr;
    auto& current = editors_[id];
    if (!current) {
        current = new PluginEditorWindow(library_, id, window_);
        connect(current, &PluginEditorWindow::notification, window_, &MainWindow::notify);
        connect(current, &PluginEditorWindow::buildRequested, this, [this, id] { build(id); });
        connect(current, &PluginEditorWindow::specificationRequested, this, [this, id] { editSpec(id); });
        connect(current, &PluginEditorWindow::externalEditorRequested, this, [this, id] { openExternal(id); });
        connect(current, &PluginEditorWindow::insertRequested, this, [this, id] { insert(id); });
    }
    current->showNormal();
    current->raise();
    current->activateWindow();
    return current;
}

void PluginStudio::editSpec(const QString& id) {
    const auto existing = library_.spec(id);
    if (!existing || library_.sourcePath(id).isEmpty()) return;
    const auto editor = editors_.value(id);
    if (editor && !editor->save()) return;
    PluginWizardDialog wizard(library_, existing, editor ? static_cast<QWidget*>(editor.data()) : window_);
    if (wizard.exec() != QDialog::Accepted) return;
    QString error;
    if (!library_.updateSpec(id, wizard.spec(), &error)) fail("Unable to update plugin", error);
}

void PluginStudio::build(const QString& id) {
    const auto editor = editors_.value(id);
    if (editor && !editor->save()) return;
    library_.buildPlugin(id, this, [this, id](const PluginBuildResult& result) {
        if (result.ok) window_->notify({NoticeSeverity::Info, "Plugin ready", id, false});
        else fail("Plugin build failed", result.error);
    });
}

void PluginStudio::copyPrompt(const QString& id) {
    const auto spec = library_.spec(id);
    if (!spec || !QApplication::clipboard()) return;
    const auto editor = editors_.value(id);
    QString source;
    if (editor) {
        source = editor->sourceText();
    } else {
        QFile file(library_.sourcePath(id));
        if (!file.open(QIODevice::ReadOnly)) return fail("Unable to read plugin source", file.errorString());
        source = QString::fromUtf8(file.readAll());
    }
    QApplication::clipboard()->setText(PluginScaffolder::generateAiPrompt(*spec, source));
    window_->notify({NoticeSeverity::Info, "AI prompt copied to clipboard", {}, false});
}

void PluginStudio::openLocation(const QString& path) {
    if (path.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        fail("Unable to open plugin path", path);
}

void PluginStudio::openExternal(const QString& id) { openLocation(library_.sourcePath(id)); }
void PluginStudio::reveal(const QString& id) { openLocation(library_.directory(id)); }

void PluginStudio::insert(const QString& id) {
    if (auto* front = window_->front()) front->controller()->place({library_.kind(), id});
}

void PluginStudio::exportPlugin(const QString& id) {
    if (!library_.spec(id)) return;
    QString destination;
    const auto moduleName = library_.moduleFileName(id);
    const auto folder = QDir::home();
    if (library_.sourcePath(id).isEmpty()) {
        const auto directory = QFileDialog::getExistingDirectory(window_, "Export module to folder", folder.path());
        if (directory.isEmpty()) return;
        destination = QDir(directory).filePath(moduleName);
    } else {
        QString selected;
        destination = QFileDialog::getSaveFileName(window_, "Export plugin", folder.filePath(id),
            "Plugin source folder (*);;GStreamer module (*.so)", &selected);
        if (selected.startsWith("GStreamer") && !destination.isEmpty())
            destination = QFileInfo(destination).dir().filePath(moduleName);
    }
    if (destination.isEmpty()) return;
    QString error;
    if (!library_.exportTo(id, destination, &error)) return fail("Unable to export plugin", error);
    window_->notify({NoticeSeverity::Info, "Plugin exported", destination, false});
}

void PluginStudio::importPlugin() {
    const auto path = QFileDialog::getOpenFileName(window_, "Import plugin", QDir::homePath(), "Custom plugins (plugin.json *.so)");
    if (path.isEmpty()) return;
    if (path.endsWith(".so")) {
        library_.importModule(path, this, [this](const QString& id, const QString& error) {
            if (id.isEmpty()) fail("Unable to import plugin", error);
            else window_->notify({NoticeSeverity::Info, "Plugin imported", id, false});
        });
        return;
    }
    QString error;
    const auto id = library_.importFrom(path, &error);
    if (id.isEmpty()) return fail("Unable to import plugin", error);
    window_->notify({NoticeSeverity::Info, "Plugin imported", id, false});
}

bool PluginStudio::closeEditor(const QString& id) {
    const auto editor = editors_.value(id);
    return !editor || editor->close();
}

bool PluginStudio::closeEditors() {
    for (const auto& id : editors_.keys()) {
        if (!closeEditor(id)) return false;
    }
    return true;
}

void PluginStudio::fail(const QString& title, const QString& error) {
    if (auto* front = window_->front()) front->log()->append(title + ": " + error);
    window_->notify({NoticeSeverity::Error, title, error, true});
}
