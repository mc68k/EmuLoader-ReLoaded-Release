// SelectMachineDialog - Select a Machine to Run the Game With (TFormArcadeSoftwareListMachineToRunGame,
// uArcadeSoftwareListSelectMachineToRunGame.pas): the MAME machines that run a software list, to
// choose the one its games run with. The machine in use is in bold; Reset selects it again.
// Show Available Only and Hide Preliminary Machines are kept in el_extras.ini
// [SelectMachineSoftwareListGame], as the original.
#pragma once

#include "ElDatabase.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormArcadeSoftwareListMachineToRunGame; }
class QStandardItemModel;
class QSortFilterProxyModel;

namespace eld {

class SelectMachineDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        MameGame game;                 // the software list game
        QIcon gameIcon;
        QString nameLine;              // the status bar's game name text
        QVector<MameGame> machines;    // the machines that run its list
        QString current;               // the machine in use ("" = none chosen yet)
        QString extrasIni;             // el_extras.ini
        std::function<QIcon(const MameGame&)> icon; // a machine's games list icon
    };
    SelectMachineDialog(const Input& in, QWidget* parent = nullptr);
    ~SelectMachineDialog() override;
    QString machine() const;           // the selected machine's name
    void selectMachine(const QString& name);
    int shownCount() const;

protected:
    void done(int r) override;

private:
    void applyFilters();
    std::unique_ptr<Ui::FormArcadeSoftwareListMachineToRunGame> ui_;
    Input in_;
    QStandardItemModel* model_ = nullptr;
    class MachinesFilter* filter_ = nullptr;
};

} // namespace eld
