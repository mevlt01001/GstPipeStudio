#pragma once
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QSplitter>
#include <QStandardPaths>
#include <QTreeView>
class PipelineFileDialog final : public QFileDialog {
  public:
    PipelineFileDialog(QWidget* parent, bool save, const QString& start) : QFileDialog(parent) {
        setOption(DontUseNativeDialog);
        setWindowTitle(save ? "Save pipeline" : "Open pipeline");
        setAcceptMode(save ? AcceptSave : AcceptOpen);
        setFileMode(save ? AnyFile : ExistingFile);
        setNameFilter("GStreamer pipeline (*.conf)");
        setDefaultSuffix("conf");
        setViewMode(Detail);
        setLabelText(FileName, "Pipeline file:");
        setLabelText(Accept, save ? "Save pipeline" : "Open pipeline");
        setSidebarUrls({QUrl::fromLocalFile(QDir::homePath()),
                        QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))});
        if (!start.isEmpty()) {
            if (QFileInfo(start).isDir())
                setDirectory(start);
            else
                selectFile(start);
        }
        resize(940, 620);
        if (auto* splitter = findChild<QSplitter*>())
            splitter->setSizes({180, 700});
        for (auto* tree : findChildren<QTreeView*>()) {
            tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        }
    }
    static QString choose(QWidget* parent, bool save, const QString& start) {
        PipelineFileDialog dialog(parent, save, start);
        return dialog.exec() == Accepted && !dialog.selectedFiles().isEmpty() ? dialog.selectedFiles().first() : QString();
    }
};
