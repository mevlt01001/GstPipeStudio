// setuppage.h – the full-canvas page shown when GStreamer is missing, too old or the helper cannot run (UI_RULES 3.5).
// It is a page, not a dialog. It only explains; the app never installs anything.
#pragma once

#include <QWidget>

#include "startupcheck.h"

class QLabel;

class SetupPage : public QWidget {
    Q_OBJECT
public:
    explicit SetupPage(QWidget* parent = nullptr);

    void show(const StartupResult& result);
    QString command() const { return command_; }

signals:
    void retry();

private:
    QLabel* title_;
    QLabel* text_;
    QLabel* hint_;
    QLabel* commandLabel_;
    QString command_;
};
