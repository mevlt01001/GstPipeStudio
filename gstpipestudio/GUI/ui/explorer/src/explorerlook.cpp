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

QIcon pipelineIcon() {
    return [] {
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
}

QIcon componentIcon() {
    return [] {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Theme::muted(), 1.4, Qt::SolidLine, Qt::RoundCap));
        p.drawRoundedRect(4, 6, 8, 5, 1, 1);
        p.drawLine(6, 3, 6, 6);
        p.drawLine(10, 3, 10, 6);
        p.drawLine(8, 11, 8, 14);
        return QIcon(pm);
    }();
}

struct Badge { QString text; QColor color; };

Badge badge(ComponentStatus status) {
    switch (status) {
    case ComponentStatus::Ready: return {"  ✓", Theme::accentHover()};
    case ComponentStatus::Pending: return {"  ●", Theme::warning()};
    case ComponentStatus::Failed: return {"  !", Theme::error()};
    case ComponentStatus::None: return {};
    }
    return {};
}

struct ActionLook { const char* name; const char* text; };
const ActionLook actions[]{
    {"explorerOpen", "Open"}, {"explorerEdit", "Edit"}, {"explorerRun", "Run pipeline"},
    {"explorerDuplicate", "Duplicate"}, {"explorerRename", "Rename"}, {"explorerDelete", "Delete"},
    {"explorerExport", "Export…"}, {"explorerImport", "Import…"}, {"explorerReveal", "Reveal in file manager"},
    {"explorerCopyPath", "Copy path"}, {"explorerNewPipeline", "New pipeline…"}, {"explorerNewProject", "New project…"},
    {"explorerRefresh", "Refresh"}, {"explorerNewPlugin", "New custom plugin…"}, {"explorerAddComponent", "Add to pipeline"},
    {"explorerEditPluginCode", "Edit code"}, {"explorerEditPluginSpec", "Edit specification…"}, {"explorerBuildPlugin", "Build && Reload"},
    {"explorerCopyAiPrompt", "Copy AI prompt"}, {"explorerOpenPluginEditor", "Open in external editor"},
    {"explorerRevealPlugin", "Reveal in file manager"},
};

}  // namespace

QString text(const ExplorerEntry& e) {
    if (e.kind == Kind::Folder) return e.name + "/";
    QString label = (e.kind == Kind::Conf && e.name.endsWith(".conf")) ? e.name.chopped(5) : e.name;
    if (e.state == State::Unsaved) return label + kDot;
    if (e.state != State::Deleted) {
        if (e.validation == Validation::Invalid) return label + kErrorBadge;
        if (e.validation == Validation::Valid) return label + kValidBadge;
    }
    return label + badge(e.componentStatus).text;
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
    const auto status = badge(e.componentStatus);
    if (status.color.isValid()) colour = status.color;
    item->setForeground(0, colour);
    QStyle* style = QApplication::style();
    if (e.kind == Kind::Folder || e.kind == Kind::Project) item->setIcon(0, style->standardIcon(QStyle::SP_DirIcon));
    else if (e.kind == Kind::Conf) item->setIcon(0, pipelineIcon());
    else if (e.componentStatus != ComponentStatus::None) item->setIcon(0, componentIcon());
    else if (e.kind == Kind::File) item->setIcon(0, style->standardIcon(QStyle::SP_FileIcon));
}

QString actionText(ExplorerAction action, const ExplorerEntry& e) {
    QString label = actions[static_cast<int>(action)].text;
    if (e.kind == Kind::Component && action == ExplorerAction::Edit) label += " " + e.origin;
    return label;
}

QString actionName(ExplorerAction action) { return actions[static_cast<int>(action)].name; }

}  // namespace ExplorerLook
