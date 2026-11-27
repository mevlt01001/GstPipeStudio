#include "plugintoolchain.h"

#include "contract.h"
#include "helperclient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include <utility>

namespace {

const QString installTools =
    "sudo apt install build-essential cmake ninja-build pkg-config libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev";

class BuildJob : public QObject {
public:
    BuildJob(PluginBuildRequest request, QObject* parent, IPluginBuilder::Output output, IPluginBuilder::Done done)
        : QObject(parent), request_(std::move(request)), output_(std::move(output)), done_(std::move(done)) {
        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.setWorkingDirectory(request_.directory);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("LC_ALL", "C");
        process_.setProcessEnvironment(environment);
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) fail(process_.errorString());
        });
        connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
            readOutput();
            if (!unfinished_.isEmpty()) {
                line(QString::fromUtf8(unfinished_));
                unfinished_.clear();
            }
            if (exit != QProcess::NormalExit || code != 0) {
                fail(stageError_);
                return;
            }
            if (steps_.empty()) verify();
            else next();
        });
    }

    void start(const QString& sdk) {
        const auto cmake = QStandardPaths::findExecutable("cmake");
        const auto pkgConfig = QStandardPaths::findExecutable("pkg-config");
        const auto compiler = QStandardPaths::findExecutable("c++");
        if (cmake.isEmpty() || pkgConfig.isEmpty() || compiler.isEmpty()) {
            fail("Custom plugins need CMake, a C++ compiler and pkg-config.\n" + installTools);
            return;
        }
        if (sdk.isEmpty()) {
            fail("GpsElementSdkConfig.cmake was not found. Reinstall GstPipeStudio or set GPS_ELEMENT_SDK_DIR to the SDK directory.");
            return;
        }
        const auto build = QDir(request_.directory).filePath("build");
        QStringList configure{"-S", request_.directory, "-B", build, "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                              "-DGpsElementSdk_DIR=" + QDir(sdk).filePath("cmake")};
        if (!QFileInfo::exists(QDir(build).filePath("CMakeCache.txt"))) {
            const bool ninja = !QStandardPaths::findExecutable("ninja").isEmpty();
            configure << "-G" << (ninja ? "Ninja" : "Unix Makefiles");
        }
        steps_ = {
            {pkgConfig, {"--exists", "gstreamer-1.0", "gstreamer-base-1.0", "gstreamer-video-1.0",
                         "gstreamer-audio-1.0", "gstreamer-allocators-1.0"},
                "The GStreamer development packages are missing.\n" + installTools},
            {cmake, configure, "CMake configuration failed."},
            {cmake, {"--build", build, "--parallel"}, "Plugin compilation failed."},
        };
        next();
    }

private:
    struct Step { QString program; QStringList arguments; QString error; };

    void next() {
        const auto step = steps_.takeFirst();
        stageError_ = step.error;
        line("$ " + step.program + " " + step.arguments.join(' '));
        process_.start(step.program, step.arguments);
    }

    void readOutput() {
        unfinished_ += process_.readAllStandardOutput();
        qsizetype end;
        while ((end = unfinished_.indexOf('\n')) >= 0) {
            line(QString::fromUtf8(unfinished_.left(end)).remove('\r'));
            unfinished_.remove(0, end + 1);
        }
    }

    void line(const QString& text) {
        result_.output += text + '\n';
        if (output_) output_(text);
    }

    void verify() {
        const QString module = QDir(request_.directory).filePath("build/libgst" + request_.id + ".so");
        PluginModules::inspect(module, this, [this, module](const std::vector<gps::FactoryEntry>& entries, const QString& error) {
            if (!error.isEmpty()) return fail(error);
            if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.factory == toStd(request_.id); }))
                return fail("The module did not register the expected factory " + request_.id + ".");
            result_.module = module;
            result_.ok = true;
            line("Verified GStreamer factory " + request_.id);
            finish();
        });
    }

    void fail(const QString& error) {
        result_.error = error;
        finish();
    }

    void finish() {
        result_.diagnostics = DiagnosticParser::parse(result_.output);
        auto done = std::move(done_);
        deleteLater();
        done(std::move(result_));
    }

    PluginBuildRequest request_;
    IPluginBuilder::Output output_;
    IPluginBuilder::Done done_;
    QProcess process_;
    QByteArray unfinished_;
    PluginBuildResult result_;
    QList<Step> steps_;
    QString stageError_;
};

}  // namespace

QList<BuildDiagnostic> DiagnosticParser::parse(const QString& output) {
    static const QRegularExpression pattern(R"(^(.+?):(\d+)(?::(\d+))?:\s+(fatal error|error|warning|note):\s+(.*)$)",
                                           QRegularExpression::MultilineOption);
    QList<BuildDiagnostic> diagnostics;
    auto match = pattern.globalMatch(output);
    while (match.hasNext()) {
        const auto row = match.next();
        diagnostics.append({row.captured(1), row.captured(4), row.captured(5).trimmed(),
                            row.captured(2).toInt(), row.captured(3).toInt()});
    }
    return diagnostics;
}

CMakePluginBuilder::CMakePluginBuilder(Options options) : options_(std::move(options)) {}

QString CMakePluginBuilder::sdkDirectory() const {
    const auto valid = [](const QString& path) {
        return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath("cmake/GpsElementSdkConfig.cmake"));
    };
    if (!options_.sdkOverride.isEmpty()) return valid(options_.sdkOverride) ? options_.sdkOverride : QString();
    const QString installed = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../sdk");
    if (valid(installed)) return installed;
    return valid(options_.sdkBuildDirectory) ? options_.sdkBuildDirectory : QString();
}

void CMakePluginBuilder::build(const PluginBuildRequest& request, QObject* context, Output output, Done done) {
    auto* job = new BuildJob(request, context, std::move(output), std::move(done));
    QTimer::singleShot(0, job, [job, sdk = sdkDirectory()] { job->start(sdk); });
}

void PluginModules::inspect(const QString& module, QObject* context, ProbeDone done) {
    auto isolated = std::make_shared<QTemporaryDir>();
    if (!isolated->isValid()) {
        done({}, "Unable to prepare a temporary GStreamer registry.");
        return;
    }
    QProcessEnvironment environment;
    environment.insert("GST_REGISTRY_1_0", isolated->filePath("registry.bin"));
    environment.insert("GST_PLUGIN_PATH_1_0", "");
    environment.insert("GST_PLUGIN_PATH", "");
    environment.insert("LD_BIND_NOW", "1");
    auto log = std::make_shared<QStringList>();
    HelperClient::run(context, "gps-inspect", {"plugin-file", module}, 30000,
        [log](const QString& line) { *log << line; },
        [isolated, log, done = std::move(done)](const HelperResult& result) {
            gps::FactoryList list;
            if (result.read(&list) && !list.elements.empty()) {
                done(std::move(list.elements), {});
                return;
            }
            gps::ErrorReply reply;
            QString error = "GStreamer could not verify the plugin module.";
            if (result.read(&reply)) error = fromStd(reply.error);
            if (!log->isEmpty()) error += "\n" + log->join('\n');
            done({}, error);
        }, {}, environment);
}

bool PluginModules::publish(const QString& source, const QString& destination, QString* error) {
    auto fail = [&](const QString& why) {
        if (error) *error = "Unable to install plugin module: " + why;
        return false;
    };
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) return fail("cannot create the plugin binary directory.");
    QFile input(source);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    while (!input.atEnd()) {
        const QByteArray data = input.read(1024 * 1024);
        if (data.isEmpty()) return fail(input.errorString());
        if (output.write(data) != data.size()) return fail(output.errorString());
    }
    const QDateTime previous = QFileInfo(destination).lastModified();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    // The native registry compares timestamps in whole seconds as well as file size.
    const auto stamp = previous.isValid() && now.toSecsSinceEpoch() <= previous.toSecsSinceEpoch() ? previous.addSecs(1) : now;
    if (!output.setFileTime(stamp, QFileDevice::FileModificationTime)) return fail(output.errorString());
    if (!output.commit()) return fail(output.errorString());
    return true;
}
