// theme.cpp – NVIDIA-like dark look from colours only (no logos, no images).
#include "theme.h"
#include "wheelguard.h"

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace {

// Qt style sheets cannot draw a triangle, so the combo box and spin box arrows are small generated images.
QString arrowFile(bool up) {
    static QTemporaryDir dir;
    QString path = dir.filePath(up ? "arrow-up.png" : "arrow-down.png");
    if (!QFile::exists(path)) {
        QImage image(10, 6, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#9a9a9a"));
        p.drawPolygon(up ? QPolygonF({{0.5, 5.5}, {9.5, 5.5}, {5, 0.5}}) : QPolygonF({{0.5, 0.5}, {9.5, 0.5}, {5, 5.5}}));
        p.end();
        image.save(path);
    }
    return path;
}

double luminance(const QColor& c) {
    auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

}  // namespace

double Theme::contrast(const QColor& a, const QColor& b) {
    double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QString Theme::styleSheet() {
    QString css = R"(
QWidget { background: @bg@; color: @text@; }
QMainWindow, QDialog { background: @bg@; }
QToolTip { background: @panel@; color: @text@; border: 1px solid @line@; padding: 3px; }

QToolBar { background: @panel@; border: none; border-bottom: 1px solid @line@; spacing: 6px; padding: 4px 8px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 5px 12px; }
QToolButton:hover { border-color: @accent@; }
QToolButton:focus { border: 2px solid @focus@; padding: 4px 11px; }
QToolButton:pressed { background: @line@; }
QToolButton:checked { background: @accent@; color: @onAccent@; border-color: @accent@; font-weight: bold; }
QToolButton:checked:hover { background: @hover@; border-color: @hover@; }
QToolButton:disabled { color: #5c5c5c; }
QToolButton#primaryAction { background: @accent@; color: @onAccent@; font-weight: bold; }
QToolButton#primaryAction:hover { background: @hover@; }
QToolButton#primaryAction:focus { border: 2px solid @text@; }
QToolButton#primaryAction:disabled { background: #36401f; color: #6c7358; }

QMenuBar { background: @panel@; border-bottom: 1px solid @line@; }
QMenuBar::item { background: transparent; padding: 5px 10px; }
QMenuBar::item:selected { background: @line@; }
QMenu { background: @panel@; border: 1px solid @line@; padding: 4px; }
QMenu::item { padding: 6px 22px; background: transparent; }
QMenu::item:selected { background: @accent@; color: @onAccent@; }
QMenu::item:disabled { color: #5c5c5c; }
QMenu::separator { height: 1px; background: @line@; margin: 4px 6px; }

QStatusBar { background: @panel@; border-top: 1px solid @line@; }
QStatusBar::item { border: none; }
QStatusBar QLabel { background: transparent; padding-left: 10px; padding-right: 10px; }

QDockWidget { titlebar-close-icon: none; color: @text@; }
QDockWidget::title { background: @panel@; border-bottom: 1px solid @line@; padding: 6px 10px; font-weight: bold; }

QTabWidget::pane { border: 1px solid @line@; background: @panel@; top: -1px; }
QTabBar::tab { background: @bg@; border: 1px solid @line@; border-bottom: none; padding: 6px 16px; margin-right: 2px; color: @muted@; }
QTabBar::tab:selected { background: @panel@; color: @text@; border-top: 2px solid @accent@; }
QTabBar::tab:hover { color: @text@; }

QPushButton { background: #303030; border: 1px solid @line@; border-radius: 3px; padding: 6px 18px; min-width: 64px; }
QPushButton:hover { border-color: @accent@; }
QPushButton:focus { border: 2px solid @focus@; padding: 5px 17px; }
QPushButton[link="true"] { background: transparent; border: none; color: @accent@; padding: 2px 8px; min-width: 0; }
QPushButton[link="true"]:hover { text-decoration: underline; }
QPushButton[link="true"]:focus { border: 2px solid @focus@; padding: 0 6px; }
QPushButton[link="true"]:disabled { color: #5c5c5c; }
QToolButton#drawerBack, QToolButton#drawerClose { border: 1px solid transparent; padding: 3px 8px; font-size: 12pt; }
QPushButton:pressed { background: @line@; }
QPushButton:disabled { color: #5c5c5c; border-color: #2c2c2c; }
QPushButton#saveButton, QPushButton[accent="true"] { background: @accent@; color: @onAccent@; border-color: @accent@; font-weight: bold; }
QPushButton#saveButton:hover, QPushButton[accent="true"]:hover { background: @hover@; border-color: @hover@; }
QPushButton#saveButton:focus, QPushButton[accent="true"]:focus { border: 2px solid @text@; padding: 5px 17px; }
QPushButton#saveButton:disabled { background: #36401f; color: #6c7358; border-color: #36401f; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: @panel@; border: 1px solid @line@; border-radius: 3px; padding: 5px 8px; min-height: 22px;
    selection-background-color: @accent@; selection-color: @onAccent@; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 2px solid @focus@; padding: 4px 7px; }
QListWidget:focus, QTableWidget:focus { border: 2px solid @focus@; }
QCheckBox::indicator:focus, QRadioButton::indicator:focus { border: 2px solid @focus@; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: #5c5c5c; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; border-left: 1px solid @line@; background: #303030; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; border-left: 1px solid @line@; background: #303030; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@arrowUp@); width: 10px; height: 6px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@arrowDown@); width: 10px; height: 6px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(@arrowDown@); width: 10px; height: 6px; }
QComboBox QAbstractItemView { background: @panel@; border: 1px solid @line@; selection-background-color: @accent@; selection-color: @onAccent@; outline: none; padding: 4px; }
QComboBox QAbstractItemView::item { min-height: 28px; padding: 4px 8px; }

QCheckBox, QRadioButton { spacing: 8px; background: transparent; padding: 3px 0; }
QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #6a6a6a; border-radius: 3px; background: @bg@; }
QCheckBox::indicator:hover { border-color: @accent@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid #6a6a6a; border-radius: 9px; background: @bg@; }
QRadioButton::indicator:hover { border-color: @accent@; }
QRadioButton::indicator:checked { background: @accent@; border-color: @accent@; }
QCheckBox:disabled, QRadioButton:disabled { color: #5c5c5c; }

QGroupBox { border: 1px solid @line@; border-radius: 4px; margin-top: 14px; padding: 12px 8px 8px 8px; background: transparent; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: @accent@; font-weight: bold; background: transparent; }

QListWidget, QTableWidget, QTreeWidget { background: @panel@; border: 1px solid @line@; alternate-background-color: #282828; outline: none; gridline-color: @line@; }
QListWidget::item:selected, QTableWidget::item:selected { background: #34420f; color: @text@; }
QListWidget::item:hover { background: #2e2e2e; }
QHeaderView::section { background: @bg@; color: @muted@; border: none; border-bottom: 1px solid @line@; border-right: 1px solid @line@; padding: 5px 8px; }
QTableCornerButton::section { background: @bg@; border: none; }

QWidget#windowChrome QToolButton { padding: 3px; }
QFileDialog QToolButton { padding: 3px; min-width: 24px; min-height: 24px; }
QToolButton#windowClose:hover { background: #a52a36; color: white; }
QWidget#windowChrome { background: #292c2f; border-bottom: 1px solid @line@; }
QLabel#windowTitle { color: @muted@; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: @bg@; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #444; min-height: 24px; border-radius: 5px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @accent@; }
QScrollBar:horizontal { background: @bg@; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: #444; min-width: 24px; border-radius: 5px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: @accent@; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QProgressBar { background: @bg@; border: 1px solid @line@; border-radius: 3px; max-height: 10px; text-align: center; }
QProgressBar::chunk { background: @accent@; }
QGraphicsView { border: none; background: @bg@; }
QLabel { background: transparent; }
QLabel#muted { color: @muted@; }
QLabel#heading, QLabel#drawerTitle { font-size: 12pt; font-weight: bold; }
QLabel#setupTitle { font-size: 18pt; font-weight: bold; }
QWidget#drawer { border-right: 1px solid @line@; }
QSplitter::handle { background: @line@; }
QFrame#separator { background: @line@; max-height: 1px; }
)";
    const QList<QPair<QString, QColor>> colours = {
        {"@onAccent@", onAccent()}, {"@focus@", focusRing()}, {"@bg@", background()}, {"@panel@", panel()}, {"@line@", line()},     {"@text@", text()},
        {"@muted@", muted()},   {"@accent@", accent()}, {"@hover@", accentHover()},
    };
    for (const auto& c : colours) css.replace(c.first, c.second.name());
    css.replace("@arrowUp@", arrowFile(true)).replace("@arrowDown@", arrowFile(false));
    return css;
}

void Theme::apply(QApplication& app) {
    Q_INIT_RESOURCE(branding);
    Q_INIT_RESOURCE(appicon);
    WheelGuard::install(app);
    app.setStyle("Fusion");
    app.setFont(QFont("DejaVu Sans", 10));
    app.setStyleSheet(styleSheet());
}
