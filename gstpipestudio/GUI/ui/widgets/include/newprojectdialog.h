// newprojectdialog.h – asks a project's name; its folder is <location>/<name>/ and its conf <name>.conf in it. The
// location starts at ~/workspace/GstPipeStudio and is remembered when the user changes it. File > New project uses it,
// and so does Save of a canvas that was never saved.
#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    QString confPath() const;  // <location>/<name>/<name>.conf

    // Asks; "" when cancelled. Nothing is made yet: the first save writes the folder.
    static QString ask(QWidget* parent);

private:
    void check();

    QLineEdit* name_;
    QLabel* where_;
    QLabel* error_;
    QString location_;
};
