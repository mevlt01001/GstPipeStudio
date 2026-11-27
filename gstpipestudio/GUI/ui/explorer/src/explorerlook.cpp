// explorerlook.cpp – see explorerlook.h.
#include "explorerlook.h"

#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QTreeWidgetItem>

#include "theme.h"

using Kind = ExplorerEntry::Kind;
using State = ExplorerEntry::State;
using Validation = ExplorerEntry::Validation;

namespace ExplorerLook {

namespace {

bool open(const ExplorerEntry& e) { return e.state == State::Open || e.state == State::Unsaved || e.state == State::Deleted; }

QString stateLine(const ExplorerEntry& e) {
    switch (e.state) {
    case State::Open: return "Open in a tab";
    case State::Unsaved: return "Unsaved changes in its tab";
    case State::Deleted: return "Deleted on disk: its tab keeps it, save to write it again";
    case State::Missing: return "Not on disk";
    default: return QString();
    }
}

QString validationLine(const ExplorerEntry& e) {
    switch (e.validation) {
    case Validation::Valid: return "Validated and ready to run";
    case Validation::Invalid: return "Validation reported a problem";
    case Validation::None: break;
    }
    return QString();
}

const QIcon& pipelineIcon() {
    static const QIcon icon = [] {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(Theme::accent(), 1.6));
        p.drawLine(5, 8, 11, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::accent());
        p.drawRoundedRect(1, 5, 5, 6, 1.5, 1.5);
        p.drawRoundedRect(10, 5, 5, 6, 1.5, 1.5);
        return QIcon(pm);
    }();
    return icon;
}

}  // namespace

QString text(const ExplorerEntry& e) {
    if (e.kind == Kind::Folder) return e.name + "/";
    QString label = (e.kind == Kind::Conf && e.name.endsWith(".conf")) ? e.name.chopped(5) : e.name;
    if (e.state == State::Unsaved) return label + kDot;
    if (e.state != State::Deleted) {
        if (e.validation == Validation::Invalid) return label + kErrorBadge;
        if (e.validation == Validation::Valid) return label + kValidBadge;
    }
    return label;
}

QString tooltip(const ExplorerEntry& e) {
    QStringList lines;
    const QString where = e.onDisk() ? e.path : QString();
    for (const QString& line : {where, e.detail, stateLine(e), validationLine(e)}) {
        if (!line.isEmpty()) lines << line;
    }
    return lines.join("\n");
}

void apply(QTreeWidgetItem* item, const ExplorerEntry& e) {
    item->setText(0, text(e));
    item->setToolTip(0, tooltip(e));
    QFont font = item->font(0);
    font.setBold(e.kind == Kind::Section || open(e));
    font.setStrikeOut(e.state == State::Deleted);
    font.setItalic(e.state == State::Missing);
    item->setFont(0, font);
    QColor colour = Theme::text();
    if (e.kind == Kind::Hint || e.state == State::Missing) colour = Theme::muted();
    else if (e.state == State::Deleted || e.validation == Validation::Invalid) colour = Theme::error();
    else if (e.state == State::Outdated) colour = Theme::warning();
    item->setForeground(0, colour);
    QStyle* style = QApplication::style();
    if (e.kind == Kind::Folder || e.kind == Kind::Project) item->setIcon(0, style->standardIcon(QStyle::SP_DirIcon));
    else if (e.kind == Kind::Conf) item->setIcon(0, pipelineIcon());
    else if (e.kind == Kind::File) item->setIcon(0, style->standardIcon(QStyle::SP_FileIcon));
}

QString actionText(ExplorerAction action, const ExplorerEntry& e) {
    const QString kind = e.kind == Kind::Component ? " " + e.origin : QString();  // "Edit template"
    switch (action) {
    case ExplorerAction::Open: return "Open";
    case ExplorerAction::Edit: return "Edit" + kind;
    case ExplorerAction::Run: return "Run pipeline";
    case ExplorerAction::Duplicate: return "Duplicate";
    case ExplorerAction::Rename: return "Rename";
    case ExplorerAction::Delete: return "Delete";
    case ExplorerAction::Export: return "Export…";
    case ExplorerAction::Import: return "Import…";
    case ExplorerAction::Reveal: return "Reveal in file manager";
    case ExplorerAction::CopyPath: return "Copy path";
    case ExplorerAction::NewPipeline: return "New pipeline…";
    case ExplorerAction::NewProject: return "New project…";
    case ExplorerAction::Refresh: return "Refresh";
    }
    return QString();
}

QString actionName(ExplorerAction action) {
    switch (action) {
    case ExplorerAction::Open: return "explorerOpen";
    case ExplorerAction::Edit: return "explorerEdit";
    case ExplorerAction::Run: return "explorerRun";
    case ExplorerAction::Duplicate: return "explorerDuplicate";
    case ExplorerAction::Rename: return "explorerRename";
    case ExplorerAction::Delete: return "explorerDelete";
    case ExplorerAction::Export: return "explorerExport";
    case ExplorerAction::Import: return "explorerImport";
    case ExplorerAction::Reveal: return "explorerReveal";
    case ExplorerAction::CopyPath: return "explorerCopyPath";
    case ExplorerAction::NewPipeline: return "explorerNewPipeline";
    case ExplorerAction::NewProject: return "explorerNewProject";
    case ExplorerAction::Refresh: return "explorerRefresh";
    }
    return QString();
}

}  // namespace ExplorerLook
