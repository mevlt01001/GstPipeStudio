// confirmdialog.h – "Şu N element silinecek" list shown before replace, cut and delete source (SPEC 8, step 5).
#pragma once

#include <QDialog>
#include <QStringList>

class ConfirmDialog : public QDialog {
    Q_OBJECT
public:
    ConfirmDialog(const QString& title, const QString& heading, const QStringList& lines, const QString& confirmText,
                  QWidget* parent = nullptr);
};
