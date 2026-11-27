// mcpinspectorpanel.cpp – see mcpinspectorpanel.h.
#include "mcpinspectorpanel.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString prettyJson(const QString& raw) {
    if (raw.trimmed().isEmpty()) return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return raw;
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QFont monoFont() {
    QFont mono("DejaVu Sans Mono", 9);
    mono.setStyleHint(QFont::Monospace);
    return mono;
}

}  // namespace

McpInspectorPanel::McpInspectorPanel(McpAuditLogger& audit, QWidget* parent)
    : QWidget(parent), audit_(audit) {
    setObjectName("mcpInspectorPanel");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(6);

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setObjectName("mcpFilterEdit");
    filterEdit_->setPlaceholderText("Filter MCP calls (client, method, tool, status)...");
    filterEdit_->setClearButtonEnabled(true);

    copyButton_ = new QPushButton("Copy MCP Config", this);
    copyButton_->setObjectName("mcpCopyConfigButton");

    openLogButton_ = new QPushButton("Open Audit Log", this);
    openLogButton_->setObjectName("mcpOpenLogButton");

    clearButton_ = new QPushButton("Clear", this);
    clearButton_->setObjectName("mcpClearButton");

    toolbar->addWidget(filterEdit_, 1);
    toolbar->addWidget(copyButton_);
    toolbar->addWidget(openLogButton_);
    toolbar->addWidget(clearButton_);
    root->addLayout(toolbar);

    auto* vSplitter = new QSplitter(Qt::Vertical, this);

    table_ = new QTableWidget(0, 6, vSplitter);
    table_->setObjectName("mcpAuditTable");
    table_->setHorizontalHeaderLabels({"Time", "Client", "Method / Tool", "Mode", "Duration (ms)", "Status"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    auto* jsonSplitter = new QSplitter(Qt::Horizontal, vSplitter);

    auto* reqPane = new QWidget(jsonSplitter);
    auto* reqLayout = new QVBoxLayout(reqPane);
    reqLayout->setContentsMargins(0, 0, 0, 0);
    reqLayout->setSpacing(2);
    reqLayout->addWidget(new QLabel("Request JSON", reqPane));
    requestView_ = new QPlainTextEdit(reqPane);
    requestView_->setObjectName("mcpRequestView");
    requestView_->setReadOnly(true);
    requestView_->setFont(monoFont());
    reqLayout->addWidget(requestView_, 1);

    auto* respPane = new QWidget(jsonSplitter);
    auto* respLayout = new QVBoxLayout(respPane);
    respLayout->setContentsMargins(0, 0, 0, 0);
    respLayout->setSpacing(2);
    respLayout->addWidget(new QLabel("Response JSON", respPane));
    responseView_ = new QPlainTextEdit(respPane);
    responseView_->setObjectName("mcpResponseView");
    responseView_->setReadOnly(true);
    responseView_->setFont(monoFont());
    respLayout->addWidget(responseView_, 1);

    vSplitter->addWidget(table_);
    vSplitter->addWidget(jsonSplitter);
    vSplitter->setStretchFactor(0, 1);
    vSplitter->setStretchFactor(1, 1);
    root->addWidget(vSplitter, 1);

    connect(filterEdit_, &QLineEdit::textChanged, this, &McpInspectorPanel::applyFilter);
    connect(copyButton_, &QPushButton::clicked, this, &McpInspectorPanel::copyConfig);
    connect(openLogButton_, &QPushButton::clicked, this, &McpInspectorPanel::openAuditLog);
    connect(clearButton_, &QPushButton::clicked, this, [this] { audit_.clear(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &McpInspectorPanel::showSelectedDetails);

    connect(&audit_, &McpAuditLogger::recorded, this, &McpInspectorPanel::appendEntry);
    connect(&audit_, &McpAuditLogger::cleared, this, &McpInspectorPanel::clearEntries);

    loadRecent();
}

QString McpInspectorPanel::mcpConfigJson() {
    const QJsonObject root{
        {"mcpServers", QJsonObject{{"gstpipestudio", QJsonObject{{"command", "gps-mcp"}}}}},
    };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void McpInspectorPanel::loadRecent() {
    clearEntries();
    for (const McpAuditEntry& entry : audit_.recent(200)) {
        appendEntry(entry);
    }
}

void McpInspectorPanel::appendEntry(const McpAuditEntry& entry) {
    const int idx = entries_.size();
    entries_.append(entry);

    const int row = table_->rowCount();
    table_->insertRow(row);

    const QString target = entry.toolOrResource.isEmpty() ? entry.method
                                                        : QString("%1 (%2)").arg(entry.method, entry.toolOrResource);
    const QString client = entry.clientVersion.isEmpty()
                               ? entry.clientName
                               : QString("%1 %2").arg(entry.clientName, entry.clientVersion);
    const QString status = entry.ok ? QStringLiteral("ok")
                                    : (entry.errorSummary.isEmpty() ? QStringLiteral("error") : entry.errorSummary);

    auto* timeItem = new QTableWidgetItem(entry.timestamp);
    timeItem->setData(Qt::UserRole, idx);
    table_->setItem(row, 0, timeItem);
    table_->setItem(row, 1, new QTableWidgetItem(client));
    table_->setItem(row, 2, new QTableWidgetItem(target));
    table_->setItem(row, 3, new QTableWidgetItem(entry.mode));
    table_->setItem(row, 4, new QTableWidgetItem(QString::number(entry.durationMs, 'f', 2)));
    table_->setItem(row, 5, new QTableWidgetItem(status));

    applyFilter();
    if (table_->currentRow() < 0 && !table_->isRowHidden(row)) {
        table_->selectRow(row);
    }
    emit countChanged(entries_.size());
}

void McpInspectorPanel::clearEntries() {
    entries_.clear();
    table_->setRowCount(0);
    requestView_->clear();
    responseView_->clear();
    emit countChanged(0);
}

void McpInspectorPanel::applyFilter() {
    const QString needle = filterEdit_->text().trimmed();
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (needle.isEmpty()) {
            table_->setRowHidden(row, false);
            continue;
        }
        const int idx = table_->item(row, 0)->data(Qt::UserRole).toInt();
        const McpAuditEntry& e = entries_.at(idx);
        const bool match = e.clientName.contains(needle, Qt::CaseInsensitive) ||
                           e.method.contains(needle, Qt::CaseInsensitive) ||
                           e.toolOrResource.contains(needle, Qt::CaseInsensitive) ||
                           e.mode.contains(needle, Qt::CaseInsensitive) ||
                           e.errorSummary.contains(needle, Qt::CaseInsensitive) ||
                           (e.ok ? QStringLiteral("ok") : QStringLiteral("error")).contains(needle, Qt::CaseInsensitive);
        table_->setRowHidden(row, !match);
    }
}

void McpInspectorPanel::showSelectedDetails() {
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0)) {
        requestView_->clear();
        responseView_->clear();
        return;
    }
    const int idx = table_->item(row, 0)->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= entries_.size()) return;
    const McpAuditEntry& e = entries_.at(idx);
    requestView_->setPlainText(prettyJson(e.requestJson));
    responseView_->setPlainText(prettyJson(e.responseJson));
}

void McpInspectorPanel::copyConfig() {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(mcpConfigJson());
    }
    emit configCopied();
}

void McpInspectorPanel::openAuditLog() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(audit_.logPath()));
}
