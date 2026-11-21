// NightModeDialog - Customize Night Mode Colors (TFormNightMode): the night profile's colors
// (nightmode\<profile>.ini), box by box: the tool bar, selection bar, search panel, panels,
// buttons, status bar, column header, splitters, image hint box and border, background, game
// documents and menus. Each change shows in the program at once when Night Mode is on; Save keeps
// it in the profile, and closing without saving goes back to the saved colors.
//   profiles   the list, Reload, Default All, New (a copy of these colors), Save, Delete
#pragma once

#include "NightMode.h"

#include <QDialog>
#include <functional>
#include <memory>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace eld {

class NightModeDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        QString folder;                                  // nightmode\ (the profiles)
        QString profile;                                 // the profile in use
        std::function<void(const QString& profile)> apply; // the program with this profile's file
        std::function<void(const NightMode&)> preview;   // the program with these colors (not saved)
    };
    explicit NightModeDialog(const Input& in, QWidget* parent = nullptr);
    ~NightModeDialog() override;

    const NightMode& colors() const { return nm_; }
    QString profile() const;
    bool modified() const { return modified_; }
    bool saveProfile();                       // Save
    void setColor(const QString& control, int tcolor); // as if chosen
    void selectProfile(const QString& name);
    bool newProfile(const QString& name, bool copyColors);
    bool deleteProfile();

protected:
    void done(int r) override;

private:
    QString fileOf(const QString& profile) const;
    void loadProfile(const QString& name);
    void show(const NightMode& nm);           // the controls from the colors
    void changed();
    void boxDefaults(QWidget* box);
    void fillProfiles(const QString& select);

    void buildPage();
    QComboBox* profiles_ = nullptr;
    QLineEdit* listImage_ = nullptr;
    QPushButton* listImageSelect_ = nullptr;
    Input in_;
    NightMode nm_;
    bool loading_ = false, modified_ = false;
    QString nm_profileShown_;
};

} // namespace eld
