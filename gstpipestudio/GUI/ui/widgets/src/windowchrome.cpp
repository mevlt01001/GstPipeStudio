#include "windowchrome.h"
#include "theme.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolButton>
#include <QWindow>
namespace {
QIcon controlIcon(QStyle::StandardPixmap type) {
    QPixmap image(18, 18);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setPen(QPen(Theme::text(), 1.5));
    switch (type) {
    case QStyle::SP_TitleBarMinButton:
        p.drawLine(3, 13, 15, 13);
        break;
    case QStyle::SP_TitleBarCloseButton:
        p.drawLine(4, 4, 14, 14);
        p.drawLine(14, 4, 4, 14);
        break;
    case QStyle::SP_TitleBarNormalButton:
        p.drawRect(6, 3, 9, 9);
        p.fillRect(3, 6, 9, 9, Theme::panel());
        p.drawRect(3, 6, 9, 9);
        break;
    default:
        p.drawRect(3, 3, 12, 12);
        break;
    }
    return QIcon(image);
}
} // namespace

WindowChrome::WindowChrome(QMainWindow* window, QMenuBar* menu) : QWidget(window), window_(window) {
    setObjectName("windowChrome");
    setMinimumHeight(40);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 2, 4, 2);
    row->setSpacing(8);
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon(":/brand/gstpipestudio.png").pixmap(26, 26));
    row->addWidget(logo);
    menu->setObjectName("applicationMenu");
    menu->setNativeMenuBar(false);
    row->addWidget(menu);
    auto* title = new QLabel(this);
    title->setObjectName("windowTitle");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    title->setAlignment(Qt::AlignCenter);
    row->addWidget(title, 1);
    connect(window, &QWidget::windowTitleChanged, title, &QLabel::setText);
    auto button = [&](const QString& name, const QString& tip, QStyle::StandardPixmap icon, auto action) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setAccessibleName(tip);
        b->setToolTip(tip);
        b->setIcon(controlIcon(icon));
        b->setFixedSize(36, 32);
        row->addWidget(b);
        connect(b, &QToolButton::clicked, this, action);
        return b;
    };
    button("windowMinimize", "Minimize", QStyle::SP_TitleBarMinButton, [this] { window_->showMinimized(); });
    auto* maximize = button("windowMaximize", "Maximize / restore", QStyle::SP_TitleBarMaxButton, [this] { toggleMaximized(); });
    connect(window, &QWidget::windowTitleChanged, maximize, [this, maximize] {
        maximize->setIcon(controlIcon(window_->isMaximized() ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
    });
    button("windowClose", "Close", QStyle::SP_TitleBarCloseButton, [this] { window_->close(); });
    qApp->installEventFilter(this);
}
void WindowChrome::toggleMaximized() {
    window_->isMaximized() ? window_->showNormal() : window_->showMaximized();
}
void WindowChrome::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && window_->windowHandle())
        window_->windowHandle()->startSystemMove();
}
void WindowChrome::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        toggleMaximized();
}
bool WindowChrome::eventFilter(QObject* object, QEvent* event) {
    if (object == window_ && event->type() == QEvent::WindowStateChange) {
        auto* button = findChild<QToolButton*>("windowMaximize");
        button->setIcon(controlIcon(window_->isMaximized() ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
    }
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget->window() != window_ || window_->isMaximized() || event->type() != QEvent::MouseButtonPress)
        return false;
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton)
        return false;
    const QPoint pos = window_->mapFromGlobal(mouse->globalPosition().toPoint());
    Qt::Edges edges;
    if (pos.x() < 5)
        edges |= Qt::LeftEdge;
    if (pos.x() >= window_->width() - 5)
        edges |= Qt::RightEdge;
    if (pos.y() < 5)
        edges |= Qt::TopEdge;
    if (pos.y() >= window_->height() - 5)
        edges |= Qt::BottomEdge;
    return edges && window_->windowHandle() && window_->windowHandle()->startSystemResize(edges);
}
