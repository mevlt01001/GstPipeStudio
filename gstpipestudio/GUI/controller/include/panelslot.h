// panelslot.h – a panel page that shows one form at a time (the Properties tab, the Caps tab): a bold title, an
// optional muted subtitle and the form; with no form, a muted hint. A form brings the panel to the front; when it goes,
// the panel gives way to the default page if it was showing.
#pragma once

#include <QString>
#include <utility>

class DrawerPage;
class Panels;
class QWidget;

class PanelSlot {
public:
    // name prefixes the labels' object names ("<name>Title", "<name>Subtitle"); hint is what an empty page says.
    PanelSlot(QString name, QString hint) : name_(std::move(name)), hint_(std::move(hint)) {}

    void attach(Panels* panels, QWidget* tab, QWidget* fallback);  // the page, and the one shown when it empties
    QWidget* tab() const { return tab_; }
    QString title() const { return title_; }  // the form's title, "" with no form

    // The form on the page, in front; false (and the page deleted) when there is no panel to show it in.
    bool present(DrawerPage* page, const QString& title, const QString& subtitle = QString());
    void close();  // the form goes, the hint comes back

private:
    void clear();

    QString name_, hint_, title_;
    Panels* panels_ = nullptr;
    QWidget* tab_ = nullptr;
    QWidget* fallback_ = nullptr;
};
