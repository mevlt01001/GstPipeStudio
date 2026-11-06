// logpanel.cpp
#include "logpanel.h"

#include <QFontDatabase>
#include <QTime>

LogPanel::LogPanel(QWidget* parent) : QPlainTextEdit(parent) {
    setObjectName("logPanel");
    setReadOnly(true);
    setMaximumBlockCount(5000);
    QFont mono("DejaVu Sans Mono", 9);
    mono.setStyleHint(QFont::Monospace);
    setFont(mono);
}

void LogPanel::append(const QString& line) {
    appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + line);
}
