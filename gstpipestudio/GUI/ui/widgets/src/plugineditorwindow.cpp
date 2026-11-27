#include "plugineditorwindow.h"

#include "pluginlibrary.h"
#include "pluginscaffolder.h"
#include "prompt.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextBlock>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

namespace {

class CppHighlighter : public QSyntaxHighlighter {
public:
    explicit CppHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString& text) override {
        static const QRegularExpression tokens(
            R"cpp(//.*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|)cpp"
            R"cpp(\b(?:auto|bool|class|const|constexpr|enum|false|if|else|for|while|namespace|override|private|public|protected|return|)cpp"
            R"cpp(static|struct|template|true|using|void|virtual)\b|)cpp"
            R"cpp(\b(?:Buffer|Caps|VideoFrame|AudioFrame|SinkPort|SrcPort|TaskDriver|ElementClassBuilder|GstFlowReturn)\b|^\s*#\w+)cpp");
        const bool boundary = text.startsWith("// BEGIN GPS_PLUGIN_") || text.startsWith("// END GPS_PLUGIN_");
        const bool region = previousBlockState() == 1 || text.startsWith("// BEGIN GPS_PLUGIN_");
        setCurrentBlockState(region && !text.startsWith("// END GPS_PLUGIN_") ? 1 : 0);
        QTextCharFormat base;
        if (region || boundary) {
            auto tint = Theme::accent();
            tint.setAlpha(32);
            base.setBackground(tint);
            setFormat(0, text.size(), base);
        }
        auto matches = tokens.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const auto token = match.captured();
            auto format = base;
            if (token.startsWith("//")) format.setForeground(Theme::muted());
            else if (token.startsWith('"') || token.startsWith('\'')) format.setForeground(Theme::accentHover());
            else format.setForeground(Theme::accent());
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }
};

QString sourceAt(const QString& path) {
    QFile source(path);
    return source.open(QIODevice::ReadOnly) ? QString::fromUtf8(source.readAll()).replace("\r\n", "\n") : QString();
}

QPushButton* button(QHBoxLayout* row, const QString& label, const char* name, const QKeySequence& shortcut = {}) {
    auto* action = new QPushButton(label);
    action->setObjectName(name);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    row->addWidget(action);
    return action;
}

}  // namespace

class PluginCodeEdit : public QPlainTextEdit {
public:
    explicit PluginCodeEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent), gutter_(new Gutter(*this)) {
        setObjectName("pluginCppSource");
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        setLineWrapMode(QPlainTextEdit::NoWrap);
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { layoutGutter(); });
        connect(this, &QPlainTextEdit::updateRequest, gutter_, qOverload<>(&QWidget::update));
        layoutGutter();
        new CppHighlighter(document());
    }

    void showDiagnostics(const QList<BuildDiagnostic>& diagnostics) {
        messages_.clear();
        QList<QTextEdit::ExtraSelection> marks;
        for (const auto& error : diagnostics) {
            const auto block = document()->findBlockByNumber(error.line - 1);
            if (!block.isValid() || error.severity == "note") continue;
            auto color = error.severity == "warning" ? Theme::warning() : Theme::error();
            const QString message = error.severity + ": " + error.message;
            messages_.insert(block.blockNumber(), {color, message});
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(block);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            color.setAlpha(44);
            selection.format.setBackground(color);
            marks.append(selection);
        }
        setExtraSelections(marks);
        gutter_->update();
    }

    void jump(int line, int column) {
        const auto block = document()->findBlockByNumber(line - 1);
        if (!block.isValid()) return;
        auto cursor = QTextCursor(block);
        cursor.setPosition(block.position() + qBound(0, column - 1, block.length() - 1));
        setTextCursor(cursor);
        centerCursor();
        setFocus();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        layoutGutter();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
            insertPlainText("    ");
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    bool viewportEvent(QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(event);
            const auto diagnostic = messages_.constFind(cursorForPosition(help->pos()).blockNumber());
            if (diagnostic != messages_.constEnd()) {
                QToolTip::showText(help->globalPos(), diagnostic->message, this);
                return true;
            }
        }
        return QPlainTextEdit::viewportEvent(event);
    }

private:
    struct Mark { QColor color; QString message; };

    class Gutter : public QWidget {
    public:
        explicit Gutter(PluginCodeEdit& edit) : QWidget(&edit), edit_(edit) {}
    private:
        void paintEvent(QPaintEvent* event) override { edit_.paintGutter(event); }
        PluginCodeEdit& edit_;
    };

    void layoutGutter() {
        const int width = 20 + fontMetrics().horizontalAdvance('9') * QString::number(blockCount()).size();
        if (viewportMargins().left() != width) setViewportMargins(width, 0, 0, 0);
        const auto rect = contentsRect();
        gutter_->setGeometry(rect.left(), rect.top(), width, rect.height());
    }

    void paintGutter(QPaintEvent*) {
        QPainter painter(gutter_);
        painter.fillRect(gutter_->rect(), palette().alternateBase());
        for (auto block = firstVisibleBlock(); block.isValid(); block = block.next()) {
            const int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
            if (top >= gutter_->height()) break;
            const auto diagnostic = messages_.constFind(block.blockNumber());
            painter.setPen(diagnostic == messages_.constEnd() ? Theme::muted() : diagnostic->color);
            if (diagnostic != messages_.constEnd())
                painter.drawText(3, top, 11, fontMetrics().height(), Qt::AlignCenter, "!");
            painter.drawText(12, top, gutter_->width() - 18, fontMetrics().height(), Qt::AlignRight,
                             QString::number(block.blockNumber() + 1));
        }
    }

    Gutter* gutter_;
    QHash<int, Mark> messages_;
};

PluginEditorWindow::PluginEditorWindow(PluginLibrary& library, QString id, QWidget* parent)
    : QMainWindow(parent, Qt::Window), library_(library), id_(std::move(id)) {
    setObjectName("pluginEditorWindow");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(950, 650);
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    auto* row = new QHBoxLayout;
    status_ = new QLabel;
    status_->setObjectName("pluginBuildStatus");
    row->addWidget(status_);
    row->addStretch();
    auto* saveButton = button(row, "Save", "pluginSaveSource", QKeySequence::Save);
    build_ = button(row, "Build && Reload", "pluginBuildReload", QKeySequence("Ctrl+B"));
    spec_ = button(row, "Edit Spec…", "pluginEditSpec");
    auto* prompt = button(row, "Copy AI Prompt", "pluginCopyPrompt");
    auto* external = button(row, "Open in External Editor", "pluginExternalEditor");
    insert_ = button(row, "Insert into Pipeline", "pluginInsert");
    layout->addLayout(row);
    diskWarning_ = new QLabel;
    diskWarning_->setWordWrap(true);
    diskWarning_->setStyleSheet("color: " + Theme::warning().name());
    diskWarning_->hide();
    layout->addWidget(diskWarning_);

    auto* split = new QSplitter(Qt::Vertical);
    code_ = new PluginCodeEdit;
    split->addWidget(code_);
    auto* tabs = new QTabWidget;
    diagnostics_ = new QListWidget;
    diagnostics_->setObjectName("pluginDiagnostics");
    tabs->addTab(diagnostics_, "Build Diagnostics");
    output_ = new QPlainTextEdit;
    output_->setObjectName("pluginBuildOutput");
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(5000);
    tabs->addTab(output_, "Build Output");
    split->addWidget(tabs);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    setCentralWidget(widget);

    connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(build_, &QPushButton::clicked, this, &PluginEditorWindow::buildRequested);
    connect(spec_, &QPushButton::clicked, this, [this] { if (save()) emit specificationRequested(); });
    connect(prompt, &QPushButton::clicked, this, &PluginEditorWindow::copyPrompt);
    connect(external, &QPushButton::clicked, this, [this] { if (save()) emit externalEditorRequested(); });
    connect(insert_, &QPushButton::clicked, this, &PluginEditorWindow::insertRequested);
    connect(code_->document(), &QTextDocument::modificationChanged, this, &PluginEditorWindow::refresh);
    connect(diagnostics_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const auto& issue = rows_.at(diagnostics_->row(item));
        if (QFileInfo(issue.file).fileName() == QFileInfo(library_.sourcePath(id_)).fileName()) code_->jump(issue.line, issue.column);
    });
    connect(&library_, &PluginLibrary::changed, this, &PluginEditorWindow::refresh);
    connect(&library_, &PluginLibrary::buildOutputLine, this, [this](const QString& id, const QString& line) {
        if (id == id_) output_->appendPlainText(line);
    });
    const auto diskChanged = [this] { QTimer::singleShot(0, this, &PluginEditorWindow::syncFromDisk); };
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, diskChanged);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, diskChanged);
    syncFromDisk();
    refresh();
    showBuild(library_.lastBuild(id_));
}

QString PluginEditorWindow::sourceText() const { return code_->toPlainText(); }

void PluginEditorWindow::watchSource() {
    const auto source = library_.sourcePath(id_);
    if (source.isEmpty()) return;
    if (!watcher_.files().contains(source) && QFile::exists(source)) watcher_.addPath(source);
    const auto folder = library_.directory(id_);
    if (!watcher_.directories().contains(folder) && QFileInfo(folder).isDir()) watcher_.addPath(folder);
}

void PluginEditorWindow::syncFromDisk() {
    watchSource();
    const auto disk = sourceAt(library_.sourcePath(id_));
    if (disk.isNull() || disk == diskText_) return;
    if (code_->document()->isModified()) {
        diskWarning_->setText("The source changed on disk. Saving will ask which version to keep.");
        diskWarning_->show();
        return;
    }
    diskText_ = disk;
    code_->setPlainText(diskText_);
    code_->document()->setModified(false);
    diskWarning_->hide();
    refresh();
}

bool PluginEditorWindow::save() {
    if (!code_->document()->isModified()) return true;
    const auto now = sourceAt(library_.sourcePath(id_));
    if (now != diskText_ && now != sourceText()) {
        const int answer = Prompt::ask(this, {"pluginDiskConflict", "Source changed on disk",
            "Which version should the editor keep?", QString(),
            {{"Save editor version", "pluginOverwrite", QMessageBox::DestructiveRole},
             {"Reload disk version", "pluginReloadFromDisk", QMessageBox::ActionRole}, Prompt::cancel()}, 2, QMessageBox::Warning});
        if (answer == 1) {
            code_->document()->setModified(false);
            syncFromDisk();
            return true;
        }
        if (answer != 0) return false;
    }
    QString error;
    if (!library_.saveSource(id_, sourceText(), &error)) {
        emit notification({NoticeSeverity::Error, "Unable to save plugin", error, true});
        return false;
    }
    diskText_ = sourceText();
    code_->document()->setModified(false);
    diskWarning_->hide();
    watchSource();
    return true;
}

void PluginEditorWindow::copyPrompt() {
    const auto spec = library_.spec(id_);
    if (!spec || !QApplication::clipboard()) return;
    QApplication::clipboard()->setText(PluginScaffolder::generateAiPrompt(*spec, sourceText()));
    emit notification({NoticeSeverity::Info, "AI prompt copied to clipboard", {}, false});
}

void PluginEditorWindow::refresh() {
    const auto spec = library_.spec(id_);
    setWindowTitle(spec ? spec->longName + " — Plugin Editor" : "Plugin Editor");
    const auto state = library_.state(id_);
    const bool active = state == PluginBuildState::Building;
    const bool unsaved = code_->document()->isModified();
    const QString status = unsaved ? QString("Modified ●") : library_.statusText(id_);
    status_->setText(status);
    QColor tint = Theme::text();
    if (unsaved || state == PluginBuildState::Outdated) tint = Theme::warning();
    else if (state == PluginBuildState::Built) tint = Theme::accentHover();
    else if (state == PluginBuildState::Failed) tint = Theme::error();
    status_->setStyleSheet("color: " + tint.name());
    if (active && !building_) {
        diagnostics_->clear();
        output_->clear();
        code_->showDiagnostics({});
    }
    if (!active && building_) showBuild(library_.lastBuild(id_));
    building_ = active;
    build_->setEnabled(spec.has_value() && !active);
    spec_->setEnabled(spec.has_value() && !active);
    insert_->setEnabled(library_.placementError(id_).isEmpty());
}

void PluginEditorWindow::showBuild(const PluginBuildResult& build) {
    output_->setPlainText(build.output + (build.error.isEmpty() ? QString() : "\n" + build.error));
    diagnostics_->clear();
    rows_ = build.diagnostics;
    QList<BuildDiagnostic> sourceIssues;
    const QString currentFile = QFileInfo(library_.sourcePath(id_)).fileName();
    for (const auto& error : rows_) {
        const QString file = QFileInfo(error.file).fileName();
        diagnostics_->addItem(QString("%1:%2:%3  %4: %5").arg(file).arg(error.line).arg(error.column).arg(error.severity, error.message));
        if (currentFile == file) sourceIssues << error;
    }
    code_->showDiagnostics(sourceIssues);
}

void PluginEditorWindow::closeEvent(QCloseEvent* event) {
    if (!code_->document()->isModified()) return QMainWindow::closeEvent(event);
    const int answer = Prompt::ask(this, {"confirmPluginClose", "Close plugin editor", "Save your C++ changes before closing?", {},
        {{"Save", "pluginCloseSave", QMessageBox::AcceptRole}, {"Discard", "pluginDiscard", QMessageBox::DestructiveRole},
         Prompt::cancel()}, 0, QMessageBox::Question});
    if (answer == 1 || (answer == 0 && save())) event->accept();
    else event->ignore();
}
