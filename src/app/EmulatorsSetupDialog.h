// EmulatorsSetupDialog - Arcade Emulators Setup (TFormArcadeEmulatorsSetup,
// uArcadeEmulatorsSetup.pas; Menu Options > MAME/Arcade > Emulators Setup): the emulator file
// and its version for each system (MAME, HBMAME).
// Everything goes to arcade\emulators.ini, the file the original uses.
//
// Changes are kept in the window until Ok. Default Settings is the exception: it opens the
// emulator's settings, so the system's file is put in place first (and taken back on Cancel).
//
// The original's AlterMAME 1 and 2 (older MAME builds to fall back on) are gone; their keys in
// emulators.ini are left as they are.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <functional>
#include <memory>

namespace Ui { class FormArcadeEmulatorsSetup; }
class QGroupBox;
class QLabel;
class QListWidget;
class QProcess;

namespace eld {

class EmulatorsSetupDialog : public QDialog {
    Q_OBJECT
public:
    struct Hooks {
        std::function<void(QProcess*)> prepare;       // hides MAME's console window (Preferences)
        std::function<void(SystemId)> openSettings;   // Emulator Default Settings for the system
    };
    EmulatorsSetupDialog(Frontend& fe, SystemId current, Hooks hooks, QWidget* parent = nullptr);
    ~EmulatorsSetupDialog() override;

    // Runs "<exe> -version" (GetArcadeEmulatorVersion) and describes the emulator.
    static EmulatorInfo detect(SystemId s, const QString& exe, const std::function<void(QProcess*)>& prepare);
    // CheckMAMEIniFile: mame.ini (and ui.ini) are written with -createconfig when missing.
    static void ensureIniFiles(Frontend& fe, SystemId s, const std::function<void(QProcess*)>& prepare);

    void reject() override;
    // As the Settings window's Emulators tab: groups that fill the tab (SettingsLayout)
    void settingsLayout();

private:
    void showSystem(int row);
    void browse();
    void clearSystem();
    void resetSystem();
    void defaultSettings();
    void accept() override;
    SystemId current() const;

    std::unique_ptr<Ui::FormArcadeEmulatorsSetup> ui_;
    Frontend& fe_;
    Hooks hooks_;
    QListWidget* systems_ = nullptr;
    QList<SystemId> ids_;
    QHash<int, EmulatorInfo> pending_, saved_; // by SystemId value
    bool applied_ = false;                       // Default Settings put a pending file in place
    int shown_ = -1;
    QGroupBox* emulatorBox_ = nullptr;   // (settingsLayout) titled with the emulator's name
    QWidget* links_ = nullptr;           // (settingsLayout) Get MAME: ... (MAME only)
    QWidget* hbLinks_ = nullptr;         // (settingsLayout) Get HBMAME: its link
    QLabel* explain_ = nullptr;          // (settingsLayout) what the buttons do, for the system chosen
};

} // namespace eld
