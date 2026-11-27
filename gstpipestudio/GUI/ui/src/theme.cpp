// theme.cpp – two palettes used by the common widget stylesheet and by canvas painters.
#include "theme.h"
#include "wheelguard.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QTemporaryDir>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr auto localStyleProperty = "gpsThemeStyle";

struct Colour {
    const char* token;
    const char* dark;
    const char* light;
    QColor value(Theme::Mode mode) const { return QColor(mode == Theme::Mode::Light ? light : dark); }
};
constexpr Colour bgColour{"@bg@", "#202224", "#f4f6f2"};
constexpr Colour panelColour{"@panel@", "#242424", "#ffffff"};
constexpr Colour boxColour{"@box@", "#2a2a2a", "#ffffff"};
constexpr Colour lineColour{"@line@", "#3a3a3a", "#bec8b9"};
constexpr Colour textColour{"@text@", "#e8e8e8", "#202820"};
constexpr Colour mutedColour{"@muted@", "#9a9a9a", "#4b5a4e"};
constexpr Colour accentColour{"@accent@", "#76B900", "#4c7000"};
constexpr Colour hoverColour{"@hover@", "#8ed000", "#3c5900"};
constexpr Colour onAccentColour{"@onAccent@", "#101010", "#ffffff"};
constexpr Colour errorColour{"@error@", "#ff6369", "#a52436"};
constexpr Colour warningColour{"@warning@", "#f5a524", "#825000"};
constexpr Colour gridColour{"@grid@", "#242424", "#e3e9de"};
constexpr Colour elementHeaderColour{"@elementHeader@", "#333333", "#e8ede5"};
constexpr Colour templateHeaderColour{"@templateHeader@", "#263526", "#e0ebd5"};
constexpr Colour linkColour{"@link@", "#5f9400", "#4c7000"};
constexpr Colour selectionColour{"@selection@", "#34420f", "#dbe9c5"};
constexpr Colour itemHoverColour{"@itemHover@", "#2e2e2e", "#edf2ea"};
constexpr Colour disabledColour{"@disabled@", "#5c5c5c", "#626e62"};
constexpr Colour disabledAccentColour{"@disabledAccent@", "#36401f", "#e1e7d9"};
constexpr Colour disabledOnAccentColour{"@disabledOnAccent@", "#6c7358", "#56654d"};
constexpr Colour buttonColour{"@button@", "#303030", "#edf1eb"};
constexpr Colour disabledBorderColour{"@disabledBorder@", "#2c2c2c", "#d6ded1"};
constexpr Colour indicatorBorderColour{"@indicatorBorder@", "#6a6a6a", "#687669"};
constexpr Colour alternateColour{"@alternate@", "#282828", "#f3f6f1"};
constexpr Colour chromeColour{"@chrome@", "#292c2f", "#e8ede5"};
constexpr Colour scrollbarColour{"@scrollbar@", "#444444", "#a4b39e"};
constexpr Colour chipColour{"@chip@", "#2d2d2d", "#edf1eb"};
constexpr Colour chipMutedColour{"@chipMuted@", "#b8b8b8", "#4b5a4e"};
constexpr Colour dangerColour{"@danger@", "#3b1c1e", "#f8e3e6"};
constexpr Colour dangerTextColour{"@dangerText@", "#111111", "#ffffff"};

QString resolve(QString css, Theme::Mode mode) {
    static constexpr Colour colours[] = {
        bgColour, panelColour, boxColour, lineColour, textColour,
        mutedColour, accentColour, hoverColour, onAccentColour, errorColour,
        warningColour, gridColour, elementHeaderColour, templateHeaderColour, linkColour,
        selectionColour, itemHoverColour, disabledColour, disabledAccentColour, disabledOnAccentColour,
        buttonColour, disabledBorderColour, indicatorBorderColour, alternateColour, chromeColour,
        scrollbarColour, chipColour, chipMutedColour, dangerColour, dangerTextColour,
    };
    for (const auto& colour : colours) {
        if (css.contains(colour.token)) css.replace(colour.token, colour.value(mode).name());
    }
    return css.replace("@focus@", accentColour.value(mode).name());
}

// Qt style sheets cannot draw a triangle, so the combo box and spin box arrows are small generated images.
QString arrowFile(bool up, const QColor& colour) {
    static QTemporaryDir dir;
    const QString path = dir.filePath(QString("arrow-%1-%2.png").arg(up ? "up" : "down", colour.name().mid(1)));
    if (!QFile::exists(path)) {
        QImage image(10, 6, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
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

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}

Theme::Mode Theme::mode() { return instance().mode_; }

QColor Theme::background() { return bgColour.value(mode()); }
QColor Theme::panel() { return panelColour.value(mode()); }
QColor Theme::box() { return boxColour.value(mode()); }
QColor Theme::line() { return lineColour.value(mode()); }
QColor Theme::text() { return textColour.value(mode()); }
QColor Theme::muted() { return mutedColour.value(mode()); }
QColor Theme::accent() { return accentColour.value(mode()); }
QColor Theme::accentHover() { return hoverColour.value(mode()); }
QColor Theme::onAccent() { return onAccentColour.value(mode()); }
QColor Theme::error() { return errorColour.value(mode()); }
QColor Theme::warning() { return warningColour.value(mode()); }
QColor Theme::grid() { return gridColour.value(mode()); }
QColor Theme::elementHeader() { return elementHeaderColour.value(mode()); }
QColor Theme::templateHeader() { return templateHeaderColour.value(mode()); }
QColor Theme::link() { return linkColour.value(mode()); }
QColor Theme::selection() { return selectionColour.value(mode()); }
QColor Theme::itemHover() { return itemHoverColour.value(mode()); }
QColor Theme::chartSeries(int series) {
    return QColor::fromHsv((series * 137 + 80) % 360, 185, mode() == Mode::Light ? 131 : 230);
}

void Theme::onChanged(QObject* context, std::function<void()> refresh) {
    connect(&instance(), &Theme::changed, context, std::move(refresh));
}

void Theme::setStyle(QWidget* widget, const QString& tokens) {
    widget->setProperty(localStyleProperty, tokens);
    const QString css = resolve(tokens, mode());
    if (widget->styleSheet() != css) widget->setStyleSheet(css);
}

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
QToolButton:disabled { color: @disabled@; }
QToolButton#primaryAction { background: @accent@; color: @onAccent@; font-weight: bold; }
QToolButton#primaryAction:hover { background: @hover@; }
QToolButton#primaryAction:focus { border: 2px solid @text@; }
QToolButton#primaryAction:disabled { background: @disabledAccent@; color: @disabledOnAccent@; }

QMenuBar { background: @panel@; border-bottom: 1px solid @line@; }
QMenuBar::item { background: transparent; padding: 5px 10px; }
QMenuBar::item:selected { background: @line@; }
QMenu { background: @panel@; border: 1px solid @line@; padding: 4px; }
QMenu::item { padding: 6px 22px; background: transparent; }
QMenu::item:selected { background: @accent@; color: @onAccent@; }
QMenu::item:disabled { color: @disabled@; }
QMenu::separator { height: 1px; background: @line@; margin: 4px 6px; }

QStatusBar { background: @panel@; border-top: 1px solid @line@; }
QStatusBar::item { border: none; }
QStatusBar QLabel { background: transparent; padding-left: 10px; padding-right: 10px; }

QDockWidget { color: @text@; }
QDockWidget::title { background: @panel@; border-bottom: 1px solid @line@; padding: 6px 10px; font-weight: bold; }

QTabWidget::pane { border: 1px solid @line@; background: @panel@; top: -1px; }
QTabBar::tab { background: @bg@; border: 1px solid @line@; border-bottom: none; padding: 6px 16px; margin-right: 2px; color: @muted@; }
QTabBar::tab:selected { background: @panel@; color: @text@; border-top: 2px solid @accent@; }
QTabBar::tab:hover { color: @text@; }
QToolButton#tabClose { padding: 0px; border: none; border-radius: 3px; color: @muted@; font-size: 11pt; }
QToolButton#tabClose:hover { color: @text@; background: @line@; }
QTabBar#documentTabs::tab { padding: 6px 8px 6px 14px; }

QPushButton { background: @button@; border: 1px solid @line@; border-radius: 3px; padding: 6px 18px; min-width: 64px; }
QPushButton:hover { border-color: @accent@; }
QPushButton:focus { border: 2px solid @focus@; padding: 5px 17px; }
QPushButton[link="true"] { background: transparent; border: none; color: @accent@; padding: 2px 8px; min-width: 0; }
QPushButton[link="true"]:hover { text-decoration: underline; }
QPushButton[link="true"]:focus { border: 2px solid @focus@; padding: 0 6px; }
QPushButton[link="true"]:disabled { color: @disabled@; }
QToolButton#drawerBack, QToolButton#drawerClose { border: 1px solid transparent; padding: 3px 8px; font-size: 12pt; }
QPushButton:pressed { background: @line@; }
QPushButton:disabled { color: @disabled@; border-color: @disabledBorder@; }
QPushButton#saveButton, QPushButton[accent="true"] { background: @accent@; color: @onAccent@; border-color: @accent@; font-weight: bold; }
QPushButton#saveButton:hover, QPushButton[accent="true"]:hover { background: @hover@; border-color: @hover@; }
QPushButton#saveButton:focus, QPushButton[accent="true"]:focus { border: 2px solid @text@; padding: 5px 17px; }
QPushButton#saveButton:disabled { background: @disabledAccent@; color: @disabledOnAccent@; border-color: @disabledAccent@; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: @panel@; border: 1px solid @line@; border-radius: 3px; padding: 5px 8px; min-height: 22px;
    selection-background-color: @accent@; selection-color: @onAccent@; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border: 2px solid @focus@; padding: 4px 7px; }
QListWidget:focus, QTableWidget:focus { border: 2px solid @focus@; }
QCheckBox::indicator:focus, QRadioButton::indicator:focus { border: 2px solid @focus@; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: @disabled@; }
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; width: 20px; border-left: 1px solid @line@; background: @button@; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-position: top right; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-position: bottom right; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@arrowUp@); width: 10px; height: 6px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@arrowDown@); width: 10px; height: 6px; }
/* A plain drop-down list (not the native popup over the current item, which this style sheet sizes wrong): every
   item is reachable, the list scrolls past maxVisibleItems. */
QComboBox { combobox-popup: 0; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(@arrowDown@); width: 10px; height: 6px; }
QComboBox QAbstractItemView { background: @panel@; border: 1px solid @line@; selection-background-color: @accent@; selection-color: @onAccent@; outline: none; padding: 4px; }
QComboBox QAbstractItemView::item { min-height: 28px; padding: 4px 8px; }

QCheckBox, QRadioButton { spacing: 8px; background: transparent; padding: 3px 0; }
QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid @indicatorBorder@; border-radius: 3px; background: @bg@; }
QCheckBox::indicator:hover { border-color: @accent@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QRadioButton::indicator { width: 18px; height: 18px; border: 1px solid @indicatorBorder@; border-radius: 9px; background: @bg@; }
QRadioButton::indicator:hover { border-color: @accent@; }
QRadioButton::indicator:checked { background: @accent@; border-color: @accent@; }
QCheckBox:disabled, QRadioButton:disabled { color: @disabled@; }

QGroupBox { border: 1px solid @line@; border-radius: 4px; margin-top: 14px; padding: 12px 8px 8px 8px; background: transparent; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: @accent@; font-weight: bold; background: transparent; }

QListWidget, QTableWidget, QTreeWidget { background: @panel@; border: 1px solid @line@;
    alternate-background-color: @alternate@; outline: none; gridline-color: @line@; }
QListWidget::item:selected, QTableWidget::item:selected { background: @selection@; color: @text@; }
QListWidget::item:hover { background: @itemHover@; }
QHeaderView::section { background: @bg@; color: @muted@; border: none; border-bottom: 1px solid @line@; border-right: 1px solid @line@; padding: 5px 8px; }
QTableCornerButton::section { background: @bg@; border: none; }

QWidget#windowChrome QToolButton { padding: 3px; }
QFileDialog QToolButton { padding: 3px; min-width: 24px; min-height: 24px; }
QToolButton#windowClose:hover { background: #a52a36; color: white; }
QWidget#windowChrome { background: @chrome@; border-bottom: 1px solid @line@; }
QLabel#windowTitle { color: @muted@; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: @bg@; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: @scrollbar@; min-height: 24px; border-radius: 5px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: @accent@; }
QScrollBar:horizontal { background: @bg@; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: @scrollbar@; min-width: 24px; border-radius: 5px; margin: 2px; }
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
    return resolve(std::move(css), mode())
        .replace("@arrowUp@", arrowFile(true, muted())).replace("@arrowDown@", arrowFile(false, muted()));
}

void Theme::apply(QApplication& app, Mode mode) {
    Q_INIT_RESOURCE(branding);
    Q_INIT_RESOURCE(appicon);
    WheelGuard::install(app);
    app.setStyle("Fusion");
    app.setFont(QFont("DejaVu Sans", 10));
    setMode(app, mode);
}

void Theme::setMode(QApplication& app, Mode mode) {
    auto& theme = instance();
    theme.mode_ = mode;
    QPalette palette;
    palette.setColor(QPalette::Window, background());
    palette.setColor(QPalette::Base, panel());
    palette.setColor(QPalette::AlternateBase, alternateColour.value(mode));
    palette.setColor(QPalette::Button, buttonColour.value(mode));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText}) {
        palette.setColor(role, text());
        palette.setColor(QPalette::Disabled, role, disabledColour.value(mode));
    }
    palette.setColor(QPalette::PlaceholderText, muted());
    palette.setColor(QPalette::ToolTipBase, panel());
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, onAccent());
    palette.setColor(QPalette::Link, accent());
    palette.setColor(QPalette::LinkVisited, accentHover());
    app.setPalette(palette);
    app.setStyleSheet(styleSheet());
    for (auto* widget : QApplication::allWidgets()) {
        const QVariant tokens = widget->property(localStyleProperty);
        if (tokens.isValid()) setStyle(widget, tokens.toString());
    }
    emit theme.changed();
}
