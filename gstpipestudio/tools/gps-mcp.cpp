// gps-mcp.cpp – standalone audited MCP JSON-RPC 2.0 stdio server for GstPipeStudio.
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "appservices.h"
#include "mcpservercore.h"

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    bool headless = false;
    QString auditDir;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--audit-dir" && i + 1 < argc) {
            auditDir = QString::fromLocal8Bit(argv[++i]);
        } else if (arg.startsWith("--audit-dir=")) {
            auditDir = arg.mid(QString("--audit-dir=").size());
        }
    }
    if (!auditDir.isEmpty()) {
        qputenv("GPS_MCP_AUDIT_DIR", auditDir.toUtf8());
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("gps-mcp");

    AppServices services;
    McpServerCore core(services, services.mcpAudit(), headless);

    std::atomic<bool> eofReached{false};
    std::thread reader([&app, &core, &eofReached] {
        std::string line;
        while (std::getline(std::cin, line)) {
            const QByteArray raw = QByteArray::fromStdString(line).trimmed();
            if (raw.isEmpty()) continue;
            QMetaObject::invokeMethod(
                &app,
                [&core, raw] {
                    QJsonParseError err;
                    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
                    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                        const QJsonObject reply{
                            {"jsonrpc", "2.0"},
                            {"id", QJsonValue::Null},
                            {"error", QJsonObject{{"code", -32700}, {"message", "Parse error"}}},
                        };
                        std::cout << QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString() << '\n';
                        std::cout.flush();
                        return;
                    }
                    if (const auto response = core.handleMessage(doc.object())) {
                        std::cout << QJsonDocument(*response).toJson(QJsonDocument::Compact).toStdString() << '\n';
                        std::cout.flush();
                    }
                },
                Qt::QueuedConnection);
        }
        eofReached.store(true);
        QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
    });

    const int rc = app.exec();
    if (eofReached.load()) {
        reader.join();
    } else {
        reader.detach();
    }
    return rc;
}
