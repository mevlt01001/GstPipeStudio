// logpanel.h – the Log tab: helper stderr and events, monospace.
#pragma once

#include <QPlainTextEdit>

class LogPanel : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

public slots:
    void append(const QString& line);
};
