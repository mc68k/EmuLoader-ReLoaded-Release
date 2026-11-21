// CategorySettingsDialog - Image Category Settings (TFormImageCategorySettings,
// uImageCategorySettings.pas; image menu or Menu Options > Images > Category Settings):
//   - which categories the image buttons cycle through (In-Game Snapshot is always on);
//   - each category's background color, or one color for all (Single Background Color);
//   - each category's folder for each system (MAME, HBMAME): MAME's ui.ini (mame.ini's
//     snapshot_directory for snapshots), or arcade\sysimagefolders_arcade.ini without a ui.ini.
// Colors and categories go to ini_files\image_category.ini, as in the original. Several folders
// can be given for MAME and HBMAME, separated by ";". Apply saves, Cancel leaves them as they were.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QIcon>
#include <array>
#include <functional>
#include <memory>

namespace Ui { class FormImageCategorySettings; }
class QListWidget;
class QGroupBox;
class QPushButton;
class QLabel;

namespace eld {

class CategorySettingsDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kCount = 17;
    static constexpr int kVideo = 1000; // the Video tile's category (ImageLayouts::Video)
    struct Values {
        std::array<int, kCount> background{};   // TColor
        std::array<bool, kCount> enabled{};
        bool video = true; // the Video category (image_category.ini [EmuLoaderRL] Video_enabled)
        QString videoOff;  // why the Video category is left out now ("": it isn't), the tile grayed
        bool single = false;
        int singleColor = 0;
    };
    CategorySettingsDialog(Frontend& fe, SystemId current, int startCategory, const Values& values,
                           std::function<QIcon(int)> categoryIcon, QWidget* parent = nullptr);
    ~CategorySettingsDialog() override;

    Values values() const { return v_; }
    bool foldersChanged() const { return foldersChanged_; }

    // As the Settings window's Pictures tab: groups that fill the tab (SettingsLayout)
    void settingsLayout();

    // UpdateCategorySettingsIni + the single color (image_category.ini)
    static void save(const QString& file, const Values& v);

protected:

private:
    void showCategory();
    void showFolder();
    void accept() override;
    void showHideAll();
    int system() const;     // index into systems_
    int category() const;

    std::unique_ptr<Ui::FormImageCategorySettings> ui_;
    Frontend& fe_;
    Values v_, saved_;
    std::function<QIcon(int)> icon_;
    QListWidget* systemsView_ = nullptr;
    QListWidget* categories_ = nullptr;
    QList<SystemId> systems_;
    QList<QStringList> folders_, original_; // per system, per category
    bool foldersChanged_ = false;
    bool filling_ = false;
    bool showAll_ = false; // the Show/Hide All label's next action
    QGroupBox* selectedBox_ = nullptr;      // (settingsLayout) "Game Snapshot", with its folder
    QPushButton* showHideButton_ = nullptr; // (settingsLayout) Show All / Hide All
    QLabel* notAvailable_ = nullptr;        // (settingsLayout) no emulator for the system
};

} // namespace eld
