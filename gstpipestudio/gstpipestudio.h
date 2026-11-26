#pragma once

#include "appservices.h"
#include "documentsession.h"
#include "mainwindow.h"

class QApplication;

class GstPipeStudio final {
public:
    int run(QApplication& application, const QString& pipelinePath);

private:
    AppServices services_;
    DocumentSessions documents_{services_};
    MainWindow window_{documents_};
};
