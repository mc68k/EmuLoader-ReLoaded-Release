// SplashCustomizeDialog - Menu Options > Customize Splash Screen (TFormStatus in its customize
// mode): the splash screen with sample texts, and beside it its settings: the alternate artwork
// and text layout, the size, the startup effect (new), the texts' colors and shadows, the
// version's place (or drag it on the picture) and the progress bar's colors.
// OK saves them in ini_files\splash.ini.
#pragma once

#include "SplashSettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace eld {

class ElColorBox;
class SplashView;

class SplashCustomizeDialog : public QDialog {
    Q_OBJECT
public:
    SplashCustomizeDialog(const QString& imagesFolder, const QString& iniFile, QWidget* parent = nullptr);
    const SplashSettings& settings() const { return s_; }
    SplashView* view() const { return view_; }
    void accept() override;

private:
    void show(const SplashSettings& s);   // the settings into the controls
    void changed();                        // the controls into the settings and the picture
    void updateProgressBoxes();
    QString iniFile_;
    SplashSettings s_;
    bool filling_ = false;
    SplashView* view_ = nullptr;
    QCheckBox *altLogo_, *altLayout_, *large_, *shadows_, *oneColor_, *insertCoin_;
    QComboBox *effect_, *scheme_;
    ElColorBox* shadowAll_;
    ElColorBox* text_[SplashSettings::TextCount];
    ElColorBox* shadow_[SplashSettings::TextCount];
    ElColorBox *backTop_, *backDown_, *border_, *barTop_, *barDown_;
    QLabel *backTopL_, *backDownL_, *borderL_, *barTopL_, *barDownL_;
    QSlider *distance_, *versionX_, *versionY_;
    QLabel *distanceV_, *versionXV_, *versionYV_;
    QPushButton* copyColors_;
};

} // namespace eld
