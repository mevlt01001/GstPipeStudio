#pragma once
#include <QDialog>
class AboutDialog final : public QDialog {
  public:
    explicit AboutDialog(const QString& runtime, QWidget* parent = nullptr);
};
