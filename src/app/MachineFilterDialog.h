// MachineFilterDialog - the Machines tool bar button's window, port of
// TFormSelectFilterSystemMega (uSelectFilterSystemMega.pas) for the MAME family:
// the machine types to list. Saved in ini_files\games_filters.ini [MachineType],
// the same keys the original uses (other keys in the file are kept).
#pragma once

#include "GamesModel.h"

#include <QDialog>
#include <memory>

namespace Ui { class FormSelectFilterSystemMega; }
class QStandardItemModel;

namespace eld {

class MachineFilterDialog : public QDialog {
    Q_OBJECT
public:
    MachineFilterDialog(const GamesFilter::MachineTypes& current, const QString& iconFolder, QWidget* parent = nullptr);
    ~MachineFilterDialog() override;
    GamesFilter::MachineTypes machineTypes() const;

    static GamesFilter::MachineTypes load(const QString& gamesFiltersFile);
    static bool save(const QString& gamesFiltersFile, const GamesFilter::MachineTypes& m);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill(const GamesFilter::MachineTypes& m);
    std::unique_ptr<Ui::FormSelectFilterSystemMega> ui_;
    QStandardItemModel* model_ = nullptr;
    GamesFilter::MachineTypes initial_;
};

} // namespace eld
