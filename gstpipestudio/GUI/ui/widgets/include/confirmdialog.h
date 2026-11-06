// confirmdialog.h – "These N elements will be removed" list shown before replace, cut and delete source: the one modal dialog.
#pragma once

#include <QDialog>
#include <QStringList>

class ConfirmDialog : public QDialog {
    Q_OBJECT
public:
    ConfirmDialog(const QString& title, const QString& heading, const QStringList& lines, const QString& confirmText,
                  QWidget* parent = nullptr);
};
