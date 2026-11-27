// onboardingdialog.h – 4-step first-launch interactive tour for GstPipeStudio.
#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QStackedWidget;

class OnboardingDialog final : public QDialog {
    Q_OBJECT
public:
    explicit OnboardingDialog(QWidget* parent = nullptr);

private:
    QWidget* buildStepPage(const QString& badge, const QString& title, const QString& summary,
                           const QStringList& highlights, QWidget* extra = nullptr);
    void updateNavigation();
    void completeTour();
    void skipTour();

    QStackedWidget* stack_ = nullptr;
    QLabel* stepLabel_ = nullptr;
    QCheckBox* dontShowCheck_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* nextButton_ = nullptr;
    QPushButton* skipButton_ = nullptr;
};
