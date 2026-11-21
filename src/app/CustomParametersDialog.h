// CustomParametersDialog - Custom Parameters (TFormCustomParameters, uCustomParameters.pas; games
// list menu > Custom Parameters > Set Game / Parent Game Parameters): the parameters MAME gets
// after the game's name when it runs, kept in <data>\arcade\custom_params\mame\<name>.txt
// (see RunExtras.h). Ok saves them (an empty edit box isn't saved), Reload reads the file again.
//
// The Machine Slots / Media Info panel on the right shows what "mame <machine> -listslots" or
// "-listmedia" says (the original reads it from the lists it keeps for every machine; here MAME
// is asked about the one machine). Edit Command runs MAME with other list options. The panel can
// be hidden with the arrow button; that is kept in EmuLoader.ini
// [Preferences] CustomParameters_ShowMachinesPanel, as the original.
//
// A MAME software list game's parameters (its own, its parent's, its list's) show how to use it
// and its requirement, and the machines that run its list (the one it runs with in bold): the
// panel shows the selected machine's slots and media. A MAME machine's parameters (the machine
// a software list runs with) apply to every game run with it.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormCustomParameters; }
class QStandardItemModel;

namespace eld {

class CustomParametersDialog : public QDialog {
    Q_OBJECT
public:
    enum For { Game, Parent, SoftwareList, Machine };
    struct Input {
        MameGame game;        // the game whose parameters these are (the parent for Parent Game,
                              // the machine for Machine)
        For what = Game;
        bool parent = false;  // Set Parent Game Parameters
        QIcon gameIcon;
        QString emuIcon;      // main_icons\emu_mame.ico
        QString file;         // <data>\arcade\custom_params\mame\<name>.txt
        // software list games
        MameGame software;              // Machine: the game it was opened for
        QVector<MameGame> machines;     // the machines that run its list
        QString machine;                // the one it runs with
        QString requirement;            // what the frontend adds for its requirement ("-cart1 scharger")
        std::function<QIcon(const MameGame&)> machineIcon;
    };
    CustomParametersDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent = nullptr);
    ~CustomParametersDialog() override;

protected:
    void done(int r) override;

private:
    void reload();
    void accept() override;
    void showSlotsMedia();
    void runCommand(const QString& options);
    void setPanelVisible(bool on);
    void layoutWindow();
    QString selectedMachine() const;
    bool softwareLines_ = false; // the usage and requirement lines are shown

    std::unique_ptr<Ui::FormCustomParameters> ui_;
    Frontend& fe_;
    SystemId sys_;
    Input in_;
    QStandardItemModel* machines_ = nullptr;
    bool panel_ = true;
    int panelWidth_ = 738; // the panel and the gap before it, while it's hidden
};

} // namespace eld
