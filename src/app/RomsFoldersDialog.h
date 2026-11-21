// RomsFoldersDialog - Menu Options > MAME/Arcade > Arcade ROMs Folders (TFormArcadeROMsFolders):
// the ROM folders of each system, as MAME's rompath gives them (mame.ini), read only. Systems
// whose emulator isn't set up are grayed out.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <memory>

class QListWidget;
namespace Ui { class FormArcadeROMsFolders; }

namespace eld {

class RomsFoldersDialog : public QDialog {
    Q_OBJECT
public:
    RomsFoldersDialog(const Frontend& fe, SystemId current, QWidget* parent = nullptr);
    ~RomsFoldersDialog() override;
    QString shownText() const; // the folders as listed

private:
    void showSystem(int row);
    std::unique_ptr<Ui::FormArcadeROMsFolders> ui_;
    const Frontend& fe_;
    QListWidget* systems_;
    QList<SystemId> ids_;
};

} // namespace eld
