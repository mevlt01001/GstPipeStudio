// documenttabs.cpp – see documenttabs.h.
#include "documenttabs.h"

#include <QFileInfo>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTabBar>

#include "appservices.h"
#include "backupservice.h"
#include "controller.h"
#include "document.h"
#include "documentbackup.h"
#include "savetarget.h"
#include "settings.h"
#include "tabclosebutton.h"

DocumentTabs::DocumentTabs(AppServices& services, QWidget* window)
    : QObject(window), services_(services), window_(window), bar_(new QTabBar(window)), pages_(new QStackedWidget(window)) {
    bar_->setObjectName("documentTabs");
    bar_->setDocumentMode(true);
    bar_->setMovable(true);
    bar_->setExpanding(false);
    bar_->setElideMode(Qt::ElideMiddle);
    bar_->setUsesScrollButtons(true);
    bar_->setContextMenuPolicy(Qt::CustomContextMenu);
    bar_->installEventFilter(this);
    pages_->setObjectName("documentPages");
    connect(bar_, &QTabBar::currentChanged, this, [this](int index) {
        if (Document* d = at(index); d && d != front_) activate(d);
    });
    connect(bar_, &QTabBar::customContextMenuRequested, this, [this](const QPoint& pos) {
        Document* d = at(bar_->tabAt(pos));
        if (!d) return;
        activate(d);
        emit menuRequested(bar_->mapToGlobal(pos));
    });
}

DocumentTabs::~DocumentTabs() {
    const QList<Document*> documents = std::exchange(docs_, {});
    front_ = nullptr;
    qDeleteAll(documents);
}

QList<Document*> DocumentTabs::all() const {
    QList<Document*> out;
    for (int i = 0; i < bar_->count(); ++i) out << at(i);
    return out;
}

Document* DocumentTabs::add(Controller* controller) {
    auto* d = new Document(services_, controller, window_);
    if (d->controller()->target().needsDestination()) {
        QList<const SaveTarget*> open;
        for (const Document* other : std::as_const(docs_)) open << &other->controller()->target();
        d->controller()->setTarget(std::make_unique<UntitledTarget>(UntitledTarget::lowestFree(open)));
    }
    pages_->addWidget(d->centre());
    docs_ << d;
    const int index = bar_->addTab(d->title());
    bar_->setTabData(index, QVariant::fromValue(static_cast<void*>(d)));
    auto* close = new TabCloseButton(bar_);
    connect(close, &QToolButton::clicked, this, [this, d] { emit closeRequested(d); });
    bar_->setTabButton(index, QTabBar::RightSide, close);
    emit added(d);
    activate(d);
    return d;
}

Document* DocumentTabs::target() { return front_ && front_->pristine() ? front_ : add(); }

Document* DocumentTabs::find(const QString& sessionKey) const {
    for (Document* d : std::as_const(docs_)) {
        if (d->shows(sessionKey) && !d->pristine()) return d;
    }
    return nullptr;
}

// A pristine tab is a blank to fill, even when it reads the same Untitled-N as a backup to reopen.
QStringList DocumentTabs::openFiles() const {
    QStringList out;
    for (const Document* d : std::as_const(docs_)) {
        if (const QString path = d->controller()->intendedTarget().path(); !path.isEmpty()) out << path;
    }
    return out;
}

Document* DocumentTabs::open(std::unique_ptr<SaveTarget> wanted, const QString& backup) {
    if (Document* d = find(wanted->sessionKey())) {
        activate(d);
        return d;
    }
    Document* d = target();
    d->opening(wanted->title());
    if (!backup.isEmpty()) d->backup()->continueFrom(backup);
    d->controller()->open(std::move(wanted), BackupService::contentOf(backup));
    refresh(d);
    return d;
}

void DocumentTabs::activate(Document* d) {
    if (!d) return;
    front_ = d;
    pages_->setCurrentWidget(d->centre());
    {
        const QSignalBlocker quiet(bar_);
        bar_->setCurrentIndex(indexOf(d));
    }
    refresh(d);
    emit activated(d);
}

void DocumentTabs::close(Document* d) {
    if (!docs_.contains(d)) return;
    d->backup()->discard();
    d->controller()->stopAll();
    docs_.removeAll(d);  // first: its Untitled-N is free for the tab that may replace it
    if (docs_.isEmpty()) add();  // the window always has a canvas
    if (d == front_) activate(at(indexOf(d) + 1) ? at(indexOf(d) + 1) : at(indexOf(d) - 1));
    bar_->removeTab(indexOf(d));
    pages_->removeWidget(d->centre());
    delete d;
}

void DocumentTabs::step(int by) {
    if (bar_->count() > 0) bar_->setCurrentIndex((bar_->currentIndex() + by + bar_->count()) % bar_->count());
}

void DocumentTabs::refresh(Document* d) {
    const int index = indexOf(d);
    if (index < 0) return;
    bar_->setTabText(index, d->title());
    bar_->setTabToolTip(index, d->path().isEmpty() ? d->title() : d->path());
    if (auto* close = qobject_cast<TabCloseButton*>(bar_->tabButton(index, QTabBar::RightSide)))
        close->setDirty(d->controller()->hasUnsavedWork());
}

void DocumentTabs::saveSession() const {
    Settings::Session session;
    for (const Document* d : all()) {
        const std::optional<Backup> tab = sessionTab(d);
        if (!tab) continue;
        if (d == front_) session.front = session.tabs.size();
        session.tabs << tab->key;
        session.backups << tab->id;
    }
    Settings::setSession(session);
}

// What the session remembers of a tab: its target with the backup it writes, or the backup it holds and could not
// load; nothing for an untitled tab without changes.
std::optional<Backup> DocumentTabs::sessionTab(const Document* d) {
    const SaveTarget& target = d->controller()->intendedTarget();  // a file still opening is that file's tab
    if (d->backup()->active()) return Backup{d->backup()->id(), target.sessionKey(), target.title()};
    if (std::optional<Backup> held = d->backup()->held()) return held;
    if (target.needsDestination()) return std::nullopt;
    return Backup{QString(), target.sessionKey(), target.title()};
}

void DocumentTabs::restoreSession() {
    const Settings::Session session = Settings::session();
    QList<Document*> opened;
    for (const Backup& tab : services_.backups().reopen(session)) opened << reopen(tab);
    if (session.front >= 0 && session.front < opened.size() && opened[session.front]) activate(opened[session.front]);
}

// Without a backup there must be a file to read: an untitled tab without changes, or a file gone, is not reopened.
Document* DocumentTabs::reopen(const Backup& tab) {
    std::unique_ptr<SaveTarget> wanted = SaveTarget::fromKey(tab.key, services_.library());
    if (!wanted || (tab.id.isEmpty() && !QFileInfo::exists(wanted->path()))) return nullptr;
    return open(std::move(wanted), tab.id);
}

bool DocumentTabs::eventFilter(QObject* watched, QEvent* event) {
    if (watched == bar_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (Document* d = at(bar_->tabAt(mouse->position().toPoint()))) emit closeRequested(d);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

Document* DocumentTabs::at(int index) const {
    return index < 0 || index >= bar_->count() ? nullptr : static_cast<Document*>(bar_->tabData(index).value<void*>());
}

int DocumentTabs::indexOf(Document* d) const {
    for (int i = 0; i < bar_->count(); ++i) {
        if (at(i) == d) return i;
    }
    return -1;
}
