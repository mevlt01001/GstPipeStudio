// onboardingdialog.cpp – see onboardingdialog.h.
#include "onboardingdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "mcpinspectorpanel.h"
#include "settings.h"
#include "theme.h"

OnboardingDialog::OnboardingDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("onboardingDialog");
    setWindowTitle("Welcome to GstPipeStudio");
    resize(700, 500);
    setMinimumSize(620, 440);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(16);

    auto* topBar = new QHBoxLayout;
    auto* brandLabel = new QLabel("GstPipeStudio Quick Tour", this);
    Theme::setStyle(brandLabel, "font-size: 13px; font-weight: 600; color: @accent@;");
    stepLabel_ = new QLabel(this);
    stepLabel_->setObjectName("onboardingStepLabel");
    Theme::setStyle(stepLabel_, "font-size: 12px; color: @muted@;");
    topBar->addWidget(brandLabel);
    topBar->addStretch(1);
    topBar->addWidget(stepLabel_);
    root->addLayout(topBar);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName("onboardingStack");

    // Step 1: Any Real-Time Stream (Visual or gst-launch)
    stack_->addWidget(buildStepPage(
        "01 / CANVAS & GST-LAUNCH",
        "Any Real-Time Stream (Visual or gst-launch)",
        "Build, import, and edit live media and sensor pipelines without losing sight of the underlying GStreamer graph.",
        {
            "Compose video, audio, sensor, and NVIDIA DeepStream pipelines visually or import any gst-launch-1.0 string.",
            "Reuse parameterized pipeline templates, auto-align complex graphs, and validate pad caps & links instantaneously.",
            "Keep your canvas, .conf files, and CLI tools (gps-run, gps-inspect, gpsconf) in sync."
        }));

    // Step 2: Live Observability
    stack_->addWidget(buildStepPage(
        "02 / TELEMETRY & DIAGNOSTICS",
        "Live Observability",
        "Inspect what is happening inside every element while your pipeline is running.",
        {
            "Monitor per-element processing latency, queue fill levels, and buffer drop rates in real time.",
            "Trace GStreamer bus warnings, caps negotiation, and build output in structured diagnostic logs.",
            "Spot bottlenecks early before deploying to edge or server hardware."
        }));

    // Step 3: Custom C++ Elements & Compiled .so Runtime
    stack_->addWidget(buildStepPage(
        "03 / ELEMENTSDK & RUNTIMESDK",
        "Custom C++ Elements (ElementSDK) & Compiled .so Runtime (RuntimeSDK)",
        "Extend GStreamer with clean C++17 and embed finished pipelines directly into native applications.",
        {
            "Author custom GstElement plugins with ElementSDK—no GObject or GStreamer boilerplate required.",
            "Compile and hot-reload plugins directly from the built-in Plugin Studio editor.",
            "Export any pipeline as a standalone .so shared library with a lightweight C/C++ RuntimeSDK host."
        }));

    // Step 4: Audited AI Driver (gps-mcp)
    auto* mcpExtra = new QFrame(this);
    Theme::setStyle(mcpExtra, "QFrame { background: @box@; border: 1px solid @line@; border-radius: 6px; }");
    auto* mcpLayout = new QVBoxLayout(mcpExtra);
    mcpLayout->setContentsMargins(12, 10, 12, 10);
    mcpLayout->setSpacing(8);

    auto* mcpRow = new QHBoxLayout;
    auto* mcpTitle = new QLabel("MCP Server Configuration (gps-mcp)", mcpExtra);
    Theme::setStyle(mcpTitle, "font-weight: 600; color: @text@; border: none;");
    auto* copyBtn = new QPushButton("Copy MCP Config", mcpExtra);
    copyBtn->setObjectName("onboardingCopyMcpButton");
    auto* copiedLabel = new QLabel(mcpExtra);
    copiedLabel->setObjectName("onboardingMcpCopiedLabel");
    Theme::setStyle(copiedLabel, "color: @accent@; font-weight: 600; border: none;");
    copiedLabel->hide();

    mcpRow->addWidget(mcpTitle);
    mcpRow->addStretch(1);
    mcpRow->addWidget(copiedLabel);
    mcpRow->addWidget(copyBtn);
    mcpLayout->addLayout(mcpRow);

    auto* configPreview = new QPlainTextEdit(McpInspectorPanel::mcpConfigJson().trimmed(), mcpExtra);
    configPreview->setReadOnly(true);
    configPreview->setMaximumHeight(92);
    Theme::setStyle(configPreview, "font-family: 'DejaVu Sans Mono'; font-size: 11px; background: @panel@; color: @text@; border: 1px solid @line@;");
    mcpLayout->addWidget(configPreview);

    connect(copyBtn, &QPushButton::clicked, this, [copiedLabel] {
        if (QClipboard* cb = QApplication::clipboard()) {
            cb->setText(McpInspectorPanel::mcpConfigJson());
        }
        copiedLabel->setText("Copied!");
        copiedLabel->show();
        QTimer::singleShot(2000, copiedLabel, &QLabel::hide);
    });

    stack_->addWidget(buildStepPage(
        "04 / AUDITED AI DRIVER",
        "Audited AI Driver (gps-mcp)",
        "Let AI coding assistants inspect, build, and validate pipelines safely through an audited Model Context Protocol server.",
        {
            "Connect Claude, Codex, Cursor, or Antigravity via gps-mcp using the configuration below.",
            "Inspect every AI JSON-RPC request and response live in the bottom MCP panel."
        },
        mcpExtra));

    root->addWidget(stack_, 1);

    auto* footer = new QHBoxLayout;
    dontShowCheck_ = new QCheckBox("Don't show on startup", this);
    dontShowCheck_->setObjectName("onboardingDontShowCheck");
    dontShowCheck_->setChecked(Settings::onboardingCompleted());

    skipButton_ = new QPushButton("Skip", this);
    skipButton_->setObjectName("onboardingSkipButton");
    backButton_ = new QPushButton("Back", this);
    backButton_->setObjectName("onboardingBackButton");
    nextButton_ = new QPushButton("Next", this);
    nextButton_->setObjectName("onboardingNextButton");
    nextButton_->setDefault(true);
    Theme::setStyle(nextButton_,
                    "QPushButton { background: @accent@; color: @onAccent@; font-weight: 600; padding: 6px 16px; border-radius: 4px; }"
                    "QPushButton:hover { background: @hover@; }");

    footer->addWidget(dontShowCheck_);
    footer->addStretch(1);
    footer->addWidget(skipButton_);
    footer->addWidget(backButton_);
    footer->addWidget(nextButton_);
    root->addLayout(footer);

    connect(skipButton_, &QPushButton::clicked, this, &OnboardingDialog::skipTour);
    connect(backButton_, &QPushButton::clicked, this, [this] {
        const int idx = stack_->currentIndex();
        if (idx > 0) stack_->setCurrentIndex(idx - 1);
    });
    connect(nextButton_, &QPushButton::clicked, this, [this] {
        const int idx = stack_->currentIndex();
        if (idx + 1 < stack_->count()) {
            stack_->setCurrentIndex(idx + 1);
        } else {
            completeTour();
        }
    });
    connect(stack_, &QStackedWidget::currentChanged, this, [this](int) { updateNavigation(); });

    updateNavigation();
}

QWidget* OnboardingDialog::buildStepPage(const QString& badge, const QString& title, const QString& summary,
                                         const QStringList& highlights, QWidget* extra) {
    auto* page = new QFrame(this);
    Theme::setStyle(page, "QFrame#onboardingPage { background: @panel@; border: 1px solid @line@; border-radius: 8px; }");
    page->setObjectName("onboardingPage");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);

    auto* badgeLabel = new QLabel(badge, page);
    Theme::setStyle(badgeLabel, "font-size: 11px; font-weight: 700; letter-spacing: 0.8px; color: @accent@; border: none;");
    layout->addWidget(badgeLabel);

    auto* titleLabel = new QLabel(title, page);
    titleLabel->setWordWrap(true);
    Theme::setStyle(titleLabel, "font-size: 18px; font-weight: 700; color: @text@; border: none;");
    layout->addWidget(titleLabel);

    auto* summaryLabel = new QLabel(summary, page);
    summaryLabel->setWordWrap(true);
    Theme::setStyle(summaryLabel, "font-size: 13px; color: @muted@; border: none;");
    layout->addWidget(summaryLabel);

    auto* listBox = new QVBoxLayout;
    listBox->setSpacing(8);
    for (const QString& item : highlights) {
        auto* row = new QLabel("•  " + item, page);
        row->setWordWrap(true);
        Theme::setStyle(row, "font-size: 13px; color: @text@; border: none; padding-left: 4px;");
        listBox->addWidget(row);
    }
    layout->addLayout(listBox);

    if (extra) layout->addWidget(extra);
    layout->addStretch(1);
    return page;
}

void OnboardingDialog::updateNavigation() {
    const int idx = stack_->currentIndex();
    const int total = stack_->count();
    stepLabel_->setText(QString("Step %1 of %2").arg(idx + 1).arg(total));
    backButton_->setEnabled(idx > 0);
    nextButton_->setText(idx + 1 >= total ? "Get Started" : "Next");
}

void OnboardingDialog::completeTour() {
    Settings::setOnboardingCompleted(true);
    accept();
}

void OnboardingDialog::skipTour() {
    if (dontShowCheck_->isChecked()) {
        Settings::setOnboardingCompleted(true);
    }
    reject();
}
