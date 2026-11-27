// mcpinspectorpanel.h – live MCP audit inspector panel showing audited requests, responses, and config copy.
#pragma once

#include <QList>
#include <QWidget>

#include "mcpaudit.h"

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class McpInspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit McpInspectorPanel(McpAuditLogger& audit, QWidget* parent = nullptr);

    int entryCount() const { return entries_.size(); }
    QString mcpConfigJson() const;

signals:
    void configCopied();
    void countChanged(int count);

private:
    void loadRecent();
    void appendEntry(const McpAuditEntry& entry);
    void clearEntries();
    void applyFilter();
    void showSelectedDetails();
    void copyConfig();
    void openAuditLog();

    McpAuditLogger& audit_;
    QList<McpAuditEntry> entries_;
    QLineEdit* filterEdit_ = nullptr;
    QPushButton* copyButton_ = nullptr;
    QPushButton* openLogButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* requestView_ = nullptr;
    QPlainTextEdit* responseView_ = nullptr;
};
