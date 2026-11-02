// updatepanel.h – the Güncelleme tab: status of the last update, the error, the pads table, missing and refused lists.
#pragma once

#include <QWidget>

class Controller;
class QLabel;
class QTableWidget;

class UpdatePanel : public QWidget {
    Q_OBJECT
public:
    explicit UpdatePanel(Controller* controller, QWidget* parent = nullptr);

    QTableWidget* table() const { return table_; }

public slots:
    void refresh();

private:
    Controller* controller_;
    QLabel* status_;
    QLabel* message_;
    QLabel* lists_;
    QTableWidget* table_;
};
