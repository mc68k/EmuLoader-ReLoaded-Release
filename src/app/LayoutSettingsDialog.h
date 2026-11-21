// LayoutSettingsDialog - Menu Options > Images > Layout Settings (uImageLayoutSettings.pas,
// FormImageLayoutSettings): the layout used; for each of the 27 layouts whether the layout buttons show it, the
// category of each image (and of each image for console/computer machines), and whether images 2
// and 3 of the dual and triple layouts are shown. Apply writes ini_files\screenshot_layouts.ini.
#pragma once

#include "ImageLayouts.h"

#include <QDialog>
#include <array>
#include <functional>
#include <memory>

class QStandardItemModel;
class QLabel;
class QCheckBox;
class QGroupBox;
class QListView;
class QPushButton;

namespace Ui { class FormImageLayoutSettings; }

namespace eld {

class LayoutSettingsDialog : public QDialog {
    Q_OBJECT
public:
    // pickCategory(current) shows the category list and returns the choice, or -1.
    LayoutSettingsDialog(const QString& layoutsFile, const QString& resourcesFolder, int currentLayout,
                         std::function<int(int)> pickCategory, QWidget* parent = nullptr);
    ~LayoutSettingsDialog() override;

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
public:
    // As the Settings window's Layouts tab: groups that fill the tab (SettingsLayout)
    void settingsLayout();
    // "Dual, Layout 1", "Quad, Layout 1 Horizontal" (sep: between the kind and the layout)
    static QString layoutName(int l, const QString& sep = QStringLiteral(", "));
    int chosen() const; // the layout selected: the one used after OK
protected:

private:
    void showLayout(int layout);
    void setIcon(QLabel* icon, QLabel* label, int category, bool video);
    void save();
    int selected() const;

    std::unique_ptr<Ui::FormImageLayoutSettings> ui_;
    QString file_, res_;
    int current_;
    std::function<int(int)> pick_;
    QStandardItemModel* model_ = nullptr;
    std::array<ImageLayouts::Settings, ImageLayouts::Count> s_;
    std::array<QLabel*, 4> icons_{}, labels_{}, iconsCC_{}, labelsCC_{};
    bool useCC_ = false; // (screenshot_layouts.ini) the console and computer categories in use
    void markItem(int l);
    bool include(int l, bool on);
    void includeAll(bool all);
    void updateInUse();
    bool autoIncluded() const;
    std::array<bool, ImageLayouts::Count> touched_{}; // the layouts whose Include was changed here
    QListView* list_ = nullptr;
    QGroupBox* panelsBox_ = nullptr;       // (settingsLayout) "Panels of Dual, Layout 1"
    QLabel* inUse_ = nullptr;               // (settingsLayout) "In use: Single"
    QCheckBox* include_ = nullptr;          // (settingsLayout) the selected layout in the < > buttons
    QCheckBox* useConsComp_ = nullptr;      // (settingsLayout) in place of the group box's own check box
    bool filling_ = false;
};

} // namespace eld
