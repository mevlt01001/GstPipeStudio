// sourcepanel.h – the left panel: a searchable list of source elements ("Kaynak ekle").
#pragma once

#include <QWidget>

class Controller;
class QLineEdit;
class QListWidget;

class SourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcePanel(Controller* controller, QWidget* parent = nullptr);

signals:
    void sourceChosen(const QString& factory);

public slots:
    void reload();  // after the catalog arrived

private:
    void filter(const QString& text);

    Controller* controller_;
    QLineEdit* search_;
    QListWidget* list_;
};
