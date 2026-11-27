#include <QtTest>

#include "boxplacement.h"
#include "diagnosticlog.h"
#include "graph.h"
#include "pipelinecompiler.h"
#include "pluginscaffolder.h"
#include "pluginspec.h"
#include "plugintoolchain.h"
#include "runtime.h"
#include "wire.h"

#ifdef GPS_HAS_RUNTIME_SDK
#include "gps_runtime/pipeline.hpp"
#endif

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

class ApplicationTest : public QObject {
    Q_OBJECT
private slots:
    void pluginSpecValidationAndJsonRoundTrip() {
        QVERIFY(!PluginSpec::idError("123bad").isEmpty());
        QVERIFY(!PluginSpec::idError("BadUpper").isEmpty());
        QVERIFY(PluginSpec::idError("good_elem1").isEmpty());

        PluginSpec spec;
        spec.id = "custom_overlay";
        spec.longName = "Custom Overlay";
        spec.description = "2-to-1 overlay with mailbox";
        spec.ports = {
            {"sink_video", PluginDirection::Sink, PluginPresence::Always, PluginCaps::VideoRaw, "RGBA"},
            {"sink_param", PluginDirection::Sink, PluginPresence::Always, PluginCaps::CustomMime, "application/x-float",
             PluginInput::Mailbox, "0.5"},
            {"src", PluginDirection::Src, PluginPresence::Always, PluginCaps::VideoRaw, "RGBA"},
        };
        spec.properties.push_back({"alpha", "Alpha", "Overlay alpha", PluginPropertyType::Float, "0.5", "0.0", "1.0",
                                   PluginMutability::Playing, true});
        QCOMPARE(spec.error(), QString());
        QCOMPARE(spec.resolvedArchetype(), PluginArchetype::GeneralElement);

        PluginSpec loaded;
        QString err;
        QVERIFY2(PluginSpec::fromJson(spec.toJson(), &loaded, &err), qPrintable(err));
        QCOMPARE(loaded.id, spec.id);
        QCOMPARE(loaded.ports.size(), 3);
        QCOMPARE(loaded.ports[1].inputMode, PluginInput::Mailbox);
        QCOMPARE(loaded.ports[1].mailboxDefault, QString("0.5"));
        QCOMPARE(loaded.properties.size(), 1);
        QCOMPARE(loaded.properties[0].name, QString("alpha"));
    }

    void pluginScaffolderPreservesUserCodeAndBuildsPrompt() {
        PluginSpec spec;
        spec.id = "my_thresh";
        spec.longName = "My Threshold";
        spec.description = "Thresholds pixels";
        spec.properties.push_back({"threshold", "Threshold", "Cut-off", PluginPropertyType::Int, "128", "0", "255",
                                   PluginMutability::Playing, true});

        QString src = PluginScaffolder::source(spec);
        QVERIFY(src.contains("BEGIN GPS_PLUGIN_SPEC"));
        QVERIFY(src.contains("END GPS_PLUGIN_SPEC"));
        QVERIFY(src.contains("threshold"));

        // Simulate user-authored algorithm outside marker regions.
        const QString userMarker = "// USER_CUSTOM_ALGO_LINE_KEEP_ME";
        src.replace("return GST_FLOW_OK;", userMarker + "\n        return GST_FLOW_OK;");

        // Update spec by adding a second property and verify user code survives.
        spec.properties.push_back({"invert", "Invert", "Invert output", PluginPropertyType::Bool, "false", {}, {},
                                   PluginMutability::Playing, false});
        QString err;
        QVERIFY2(PluginScaffolder::updateSpecRegions(spec, &src, &err), qPrintable(err));
        QVERIFY(src.contains(userMarker));
        QVERIFY(src.contains("invert"));

        const QString prompt = PluginScaffolder::generateAiPrompt(spec, src);
        QVERIFY(prompt.contains("my_thresh"));
        QVERIFY(prompt.contains("GPS_PLUGIN_SPEC"));
    }

    void diagnosticParserParsesCompilerErrors() {
        const QString output =
            "/tmp/my_elem/my_elem.cpp:42:17: error: 'unknown_var' was not declared in this scope\n"
            "/tmp/my_elem/my_elem.cpp:12:5: warning: unused variable 'x' [-Wunused-variable]\n";
        const QList<BuildDiagnostic> diags = DiagnosticParser::parse(output);
        QCOMPARE(diags.size(), 2);
        QCOMPARE(diags[0].line, 42);
        QCOMPARE(diags[0].column, 17);
        QCOMPARE(diags[0].severity, QString("error"));
        QCOMPARE(diags[1].severity, QString("warning"));
    }

    void diagnosticLogIsolatesChannels() {
        DiagnosticLog log;
        QSignalSpy spy(&log, &DiagnosticLog::recorded);
        log.info(LogChannel::PluginBuild, "my_elem", "Building...");
        log.error(LogChannel::Mcp, "gps-mcp", "Invalid tool args");
        QCOMPARE(spy.count(), 2);
        QCOMPARE(log.records(LogChannel::PluginBuild).size(), 1);
        QCOMPARE(log.records(LogChannel::Mcp).size(), 1);
        QCOMPARE(log.records(LogChannel::System).size(), 0);
        QVERIFY(log.records(LogChannel::PluginBuild)[0].formatted().contains("[plugin:my_elem]"));
    }

    void boxPlacementAvoidsOverlapAndMakesRoomForQueue() {
        gps::Graph g;
        const std::string srcName = g.addElement("videotestsrc", {}, 40, 40);
        const std::string sinkName = g.addElement("fakesink", {}, 300, 40);
        QVERIFY(g.linkExisting(srcName, "src", sinkName, "sink"));

        const std::string newName = g.addElement("identity", {}, 40, 40);
        BoxPlacement::placeNew(g, {newName}, [](const std::string&) { return 100.0; });
        QVERIFY(g.find(newName)->y > 40.0);

        const std::string qName = g.insertQueue(srcName, "src");
        QVERIFY(!qName.empty());
        BoxPlacement::makeRoomForQueue(g, srcName, qName);
        QVERIFY(g.find(sinkName)->x > g.find(qName)->x);
    }

    void elementDiagnosticWireRoundTrip() {
        gps::UpdateResult res;
        res.status = "error";
        res.diagnostics.push_back({"conv_0", "sink", "caps mismatch", true});
        const std::string json = gps::toJson(res);
        gps::UpdateResult decoded;
        QVERIFY(gps::fromJson(json, &decoded));
        QCOMPARE(decoded.diagnostics.size(), size_t(1));
        QCOMPARE(QString::fromStdString(decoded.diagnostics[0].element), QString("conv_0"));
        QCOMPARE(QString::fromStdString(decoded.diagnostics[0].pad), QString("sink"));
        QVERIFY(decoded.diagnostics[0].isError);
    }

    void pipelineCompilerProducesDualPersonalitySharedLibrary() {
        QVERIFY(!PipelineCompiler::validateModuleName("123bad").isEmpty());
        QVERIFY(!PipelineCompiler::validateModuleName("BadUpper").isEmpty());
        QVERIFY(PipelineCompiler::validateModuleName("test_export").isEmpty());

#ifndef GPS_HAS_RUNTIME_SDK
        QSKIP("GpsRuntimeSdk::Runtime not linked in this build");
#else
        if (QStandardPaths::findExecutable("cmake").isEmpty()) QSKIP("cmake not available");

        gps::Graph g;
        const std::string src = g.addSource("videotestsrc", {{"num-buffers", "3"}});
        const std::string sink = g.addAfter(src, "src", "fakesink", "sink");
        QVERIFY(!sink.empty());
        g.setExposed({{"frames", src, "num-buffers"}});

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        PipelineExportOptions opts;
        opts.moduleName = "test_export";
        opts.outputDir = tempDir.path();
        opts.version = "0.2.0";
        opts.description = "Unit test exported pipeline";

        const PipelineExportResult res = PipelineCompiler::compile(g, opts);
        QVERIFY2(res.ok, qPrintable(res.error + "\n" + res.output));
        QVERIFY(QFileInfo::exists(res.soPath));
        QVERIFY(QFileInfo::exists(res.integrationDocPath));
        QVERIFY(QFileInfo::exists(res.exampleHostPath));
        QVERIFY(QFileInfo::exists(QDir(tempDir.path()).filePath("include/gps_runtime/abi.h")));
        QVERIFY(QFileInfo::exists(QDir(tempDir.path()).filePath("include/gps_runtime/pipeline.hpp")));

        // 1. Load via gps::runtime::Pipeline C++ wrapper
        std::string err;
        auto pipe = gps::runtime::Pipeline::open(res.soPath.toStdString(), &err);
        QVERIFY2(pipe.has_value(), err.c_str());
        QCOMPARE(QString::fromStdString(pipe->manifest().name), QString("test_export"));
        QCOMPARE(QString::fromStdString(pipe->manifest().version), QString("0.2.0"));
        QCOMPARE(pipe->manifest().params.size(), size_t(1));
        QCOMPARE(QString::fromStdString(pipe->manifest().params[0].name), QString("frames"));

        QVERIFY2(pipe->setParam("frames", "2", &err), err.c_str());
        QCOMPARE(QString::fromStdString(pipe->getParam("frames").value_or("")), QString("2"));
        QVERIFY2(pipe->start(&err), err.c_str());
        QVERIFY2(pipe->waitForEos(5000, &err), err.c_str());
        pipe->stop();

        // 2. Verify native GStreamer plugin personality via gst-inspect-1.0
        const QString gstInspect = QStandardPaths::findExecutable("gst-inspect-1.0");
        if (!gstInspect.isEmpty()) {
            QProcess proc;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("GST_PLUGIN_PATH", tempDir.path());
            proc.setProcessEnvironment(env);
            proc.start(gstInspect, {"test_export"});
            QVERIFY(proc.waitForFinished(15000));
            const QString inspectOut = QString::fromUtf8(proc.readAllStandardOutput());
            QVERIFY2(proc.exitCode() == 0, qPrintable(inspectOut + "\n" + QString::fromUtf8(proc.readAllStandardError())));
            QVERIFY(inspectOut.contains("frames"));
        }
#endif
    }
};

QTEST_MAIN(ApplicationTest)
#include "application_test.moc"
