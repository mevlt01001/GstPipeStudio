// flows.h – the dialog sequences of SPEC 8 (Seçici -> PropertyDialog -> sink pad -> onay -> güncelleme -> CapsDialog).
// Dialogs are opened with open() (not exec()), so the event loop keeps running and tests can drive them.
// Cancel at any step leaves the Plan exactly as it was; the Controller is only called when the last dialog says Save.
#pragma once

#include <QObject>
#include <QPoint>
#include <functional>

#include "controller.h"

class QDialog;
class QWidget;

class Flows : public QObject {
    Q_OBJECT
public:
    Flows(Controller* controller, QWidget* window);

    // Kaynak ekle
    void addSource(const QString& factory);
    void chooseSource();
    // Left click on a free src pad
    void fromFreePad(const QString& element, const QString& identity);
    // Right click on a linked pad or a link
    void showLinkMenu(const QString& element, const QString& identity, const QPoint& at);
    void editCaps(const QString& element, const QString& identity);
    void replaceBehind(const QString& element, const QString& identity);
    void cutLink(const QString& element, const QString& identity);
    void addQueue(const QString& element, const QString& identity);
    // Element menu
    void showElementMenu(const QString& element, const QPoint& at);
    void editProperties(const QString& element);
    void deleteSource(const QString& element);

private:
    enum class Mode { Add, Replace };

    void present(QDialog* dialog, std::function<void()> accepted);
    void confirmRemoval(const gps::Removal& removal, const QString& confirmText, std::function<void()> accepted);
    void newElement(Mode mode, const QString& element, const QString& identity, const QString& factory);
    void chooseSinkPad(const QList<PadTemplateInfo>& choices, std::function<void(const QString&)> chosen);
    // Runs an edit, then opens the CapsDialog on the link from (element, identity) when the update succeeded.
    void editThenCaps(const QString& element, const QString& identity, std::function<bool()> edit);
    void openCaps(const QString& element, const QString& identity, bool always);

    Controller* controller_;
    QWidget* window_;
};
