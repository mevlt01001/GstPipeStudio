#include "documentsession.h"

#include <algorithm>

DocumentSession::DocumentSession(AppServices& services, Controller* borrowed)
    : ownedController_(borrowed ? nullptr : std::make_unique<Controller>(services)),
      controller_(borrowed ? *borrowed : *ownedController_), backup_(services.backups(), controller_, services.timing().backupDelayMs) {}

DocumentSession& DocumentSessions::open(Controller* borrowed) {
    auto session = std::unique_ptr<DocumentSession>(new DocumentSession(services_, borrowed));
    DocumentSession& opened = *session;
    sessions_.push_back(std::move(session));
    return opened;
}

void DocumentSessions::close(DocumentSession& session) {
    auto found = std::find_if(sessions_.begin(), sessions_.end(), [&session](const auto& entry) { return entry.get() == &session; });
    if (found != sessions_.end()) sessions_.erase(found);
}
