#pragma once

#include <memory>
#include <vector>

#include "controller.h"
#include "documentbackup.h"

class DocumentSessions;

class DocumentSession final {
public:
    Controller& controller() { return controller_; }
    DocumentBackup& backup() { return backup_; }

private:
    friend class DocumentSessions;
    DocumentSession(AppServices& services, Controller* borrowed);

    std::unique_ptr<Controller> ownedController_;
    Controller& controller_;
    DocumentBackup backup_;
};

class DocumentSessions final {
public:
    explicit DocumentSessions(AppServices& services) : services_(services) {}

    AppServices& services() const { return services_; }
    DocumentSession& open(Controller* borrowed = nullptr);
    void close(DocumentSession& session);

private:
    AppServices& services_;
    std::vector<std::unique_ptr<DocumentSession>> sessions_;
};
