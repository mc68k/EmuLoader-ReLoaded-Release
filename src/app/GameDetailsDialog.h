// GameDetailsDialog - Game Details (TFormGameDetails, uGameDetails.pas; games list menu): what
// is known about the selected game on the left (from the games list and MAME), and its ROM and
// disk files on the right, each found or missing in the ROM folders.
//
// The original reads the ROMs from the arcade\games\*.elrom file it writes when it creates the
// games list; this port asks MAME for the one machine (mame -listxml <name>) when the window
// opens, so the list always matches the MAME in use. ROMs are checked by CRC in the set's .zip
// (a .7z counts as found when it's there), CHDs by their file.
#pragma once

#include "ElDatabase.h"
#include "Frontend.h"
#include "MachineInfo.h"

#include <QDialog>
#include <QIcon>
#include <memory>

namespace Ui { class FormGameDetails; }
class QGridLayout;
class QStandardItemModel;
class QLabel;

namespace eld {

class GameDetailsDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        MameGame game;
        QString nameLine;            // the status bar's game name text
        QIcon gameIcon;              // the games list icon
        QString emuIcon;             // main_icons\emu_mame.ico / emu_hbmame.ico
        QString lastPlayed, playtime; // play statistics, when known
        QString players;              // Number of Players, as the games list shows it
    };
    GameDetailsDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent = nullptr);
    ~GameDetailsDialog() override;

private:
    void runMame();
    void readSoftware();
    QString set_, parentSet_; // where its files are: "pacman", or "nes/smb" for a software list game
    void fillLeft();
    void fillRoms();
    void layoutWindow();
    // AddEntry2: a title and its value on the left; state colors the value (SetLabelColor).
    enum State { Normal = -1, Missing = 0, Found = 1, Partly = 2, Merged = 3, FileKind = -5, Unknown = 21 };
    void addEntry(const QString& title, const QString& value, int state = Normal, const QString& tip = {});
    QLabel* valueLabel(const QString& text, int state);
    static int statusState(int status); // GetDriverStatusImageIndex
    QString setFileText(const QString& set, const QString& ext);
    static QString titleText(const QString& title);

    std::unique_ptr<Ui::FormGameDetails> ui_;
    Frontend& fe_;
    SystemId sys_;
    Input in_;
    MachineInfo m_;
    QHash<QString, MachineInfo> devices_;
    QString mameError_;
    std::unique_ptr<SetFiles> files_;
    QStandardItemModel* model_ = nullptr;
    QWidget* left_ = nullptr;     // the titles and their values
    QGridLayout* grid_ = nullptr;
};

} // namespace eld
