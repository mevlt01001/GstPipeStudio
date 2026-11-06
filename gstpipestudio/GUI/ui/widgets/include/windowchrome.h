#pragma once
#include <QWidget>
class QMainWindow;
class QMenuBar;
class WindowChrome final : public QWidget {
  public:
    WindowChrome(QMainWindow* window, QMenuBar* menu);

  protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

  private:
    QMainWindow* window_;
    void toggleMaximized();
};
