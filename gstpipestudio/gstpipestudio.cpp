#include "gstpipestudio.h"

#include <QApplication>

#include "logpanel.h"
#include "notice.h"

int GstPipeStudio::run(QApplication& application, const QString& pipelinePath) {
    window_.show();
    window_.restoreSession();
    if (!pipelinePath.isEmpty()) window_.openFile(pipelinePath);
    return application.exec();
}

void GstPipeStudio::reportError(const QString& title, const QString& detail) {
    window_.logPanel()->append(detail.isEmpty() ? title : title + ": " + detail);
    window_.notify({NoticeSeverity::Error, title, detail, true});
}
