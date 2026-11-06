#include "aboutdialog.h"
#include "buildinfo.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMovie>
#include <QPushButton>
#include <QVBoxLayout>
AboutDialog::AboutDialog(const QString& runtime, QWidget* parent) : QDialog(parent) {
    setObjectName("aboutDialog");
    setWindowTitle("About " + BuildInfo::title());
    resize(580, 460);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    auto* logo = new QLabel(this);
    logo->setAlignment(Qt::AlignCenter);
    auto* movie = new QMovie(":/brand/animation.gif", QByteArray(), this);
    movie->jumpToFrame(0);
    movie->setScaledSize(movie->frameRect().size().scaled(QSize(480, 180), Qt::KeepAspectRatio));
    logo->setMovie(movie);
    movie->start();
    layout->addWidget(logo);
    auto* heading = new QLabel("<h2>" + BuildInfo::title() + "</h2>Build, connect and understand your media pipeline.", this);
    layout->addWidget(heading);
    const QString details = QString("Version %1 · %2\nCommit %3\nQt %4 · %5\nExecutable: %6")
                                .arg(BuildInfo::version(), BuildInfo::channel(), BuildInfo::revision(), qVersion(), runtime,
                                     QCoreApplication::applicationFilePath());
    auto* info = new QLabel(details, this);
    info->setObjectName("buildDetails");
    info->setWordWrap(true);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(info);
    auto* links = new QLabel("<a style=\"color:#8ed000\" href=\"https://github.com/mevlt01001/GstPipeStudio\">Project & contributions</a> "
                             "· <a style=\"color:#8ed000\" href=\"https://github.com/mevlt01001/GstPipeStudio/issues\">Report a "
                             "problem</a><br>Open source · MIT License",
                             this);
    links->setOpenExternalLinks(true);
    layout->addWidget(links);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* copy = buttons->addButton("Copy build details", QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, [details] { QApplication::clipboard()->setText(details); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
