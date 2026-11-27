// notificationoverlay.h – non-blocking floating toast notifications anchored at the bottom-right of MainWindow.
#pragma once

#include <QList>
#include <QWidget>

#include "notice.h"

class QEvent;
class QVBoxLayout;

class NotificationOverlay : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMaxVisible = 3;

    explicit NotificationOverlay(QWidget* parent);

    void push(const Notice& notice);
    int count() const { return cards_.size(); }
    void dismissAll();

signals:
    void viewLogRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class ToastCard;

    void removeCard(ToastCard* card);
    ToastCard* evictionCandidate(NoticeSeverity incoming) const;
    void reposition();

    QVBoxLayout* layout_ = nullptr;
    QList<ToastCard*> cards_;
};
