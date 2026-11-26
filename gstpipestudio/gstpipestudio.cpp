#include "gstpipestudio.h"

#include <QApplication>

int GstPipeStudio::run(QApplication& application, const QString& pipelinePath) {
    window_.show();
    window_.restoreSession();
    if (!pipelinePath.isEmpty()) window_.openFile(pipelinePath);
    return application.exec();
}
