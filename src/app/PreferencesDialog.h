// PreferencesDialog - port of EmuLoader's TFormPreferences (uPreferences.pas). The window
// itself is never shown: its controls, with their code and ini keys, make four tabs of the
// Settings window (takePages: General, Games List, Game Info and Documents, Colors). The
// settings are saved when it's closed with accept() or reject(), as the original saved them
// when its window closed (Settings closes it with OK; Cancel closes it with done()).
//
// Added for this port: "Enable Console and Computer Support" ([EmuLoaderRL]
// ConsoleComputerSupport). Off = arcade only: console/computer machines aren't listed and
// the Machines button and the console/computer menu items are hidden.
#pragma once

#include "Frontend.h"
#include "LightMode.h"

#include <QComboBox>
#include <QDialog>
#include <QPointer>
#include <memory>
#include <vector>

namespace Ui { class FormPreferences; }
class QCheckBox;
class QStandardItemModel;

namespace eld {

struct SettingEntry;

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(Frontend& fe, QWidget* parent = nullptr);
    ~PreferencesDialog() override;

    static bool consoleComputerSupport(Frontend& fe);

    struct Setting { const char* control; const char* section; const char* key; bool def; };
    static const std::vector<Setting>& settings();

signals:
    void iconsFolderChanged(); // MAMu_ icons: read the folder again
    void lightModeChanged(const eld::LightMode& lm); // live preview, as the original

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void reject() override; // Esc closes and applies too
    void accept() override; // (however it's closed, the settings are kept)

public:
    // The Settings window's tabs, made from this window's controls (they move there)
    QList<QPair<QString, QWidget*>> takePages();
    // Find a Setting: the tabs' settings (after takePages), and one of them highlighted.
    void addSettingsTo(QList<SettingEntry>& out);
    QWidget* reveal(const QString& objectName); // the control, or nullptr
    QWidget* find(const QString& objectName) const;

private:
    void retext(); // the settings' texts and explanations (PreferencesPages.cpp)
    void setupPage2();
    void fillPage2();
    void updateHintLabels();
    LightMode lm_;
    void saveSettings();
    void setupGameDocs();
    QStandardItemModel* docsModel_ = nullptr;
    std::unique_ptr<Ui::FormPreferences> ui_;
    Frontend& fe_;
    QCheckBox* consoleComputer_ = nullptr;
    QCheckBox* checkForUpdates_ = nullptr; // [EmuLoaderRL] CheckForUpdates (MainWindowUpdates.cpp)
    QComboBox* language_ = nullptr;
    QString initialLanguage_;
    QList<QPair<QString, QPointer<QWidget>>> taken_; // the tabs and their titles
};

} // namespace eld
