// validationindicator.h – bottom right of the status bar (UI_RULES 3.1 and 3.2): the spinner, "Validating…", the
// elapsed seconds and Cancel; after 60 s the question Wait / Cancel / Request support; after a helper crash the
// message with Request support / Retry. Hidden otherwise. It reads the Controller; it decides nothing.
#pragma once

#include <QWidget>

class Controller;
class QLabel;
class QPushButton;
class Spinner;

class ValidationIndicator : public QWidget {
    Q_OBJECT
public:
    enum Mode { Hidden, Validating, Slow, Stopped };

    explicit ValidationIndicator(Controller* controller, QWidget* parent = nullptr);
    Mode mode() const { return mode_; }

public slots:
    void refresh();

private:
    Controller* controller_;
    Mode mode_ = Hidden;
    Spinner* spinner_;
    QLabel* label_;
    QLabel* elapsed_;
    QPushButton* wait_;
    QPushButton* cancel_;
    QPushButton* support_;
    QPushButton* retry_;
};
