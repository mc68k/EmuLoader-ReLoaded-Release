// CustomizeListDialog - Customize MAME Software List (TFormArcadeSoftwareListCustomize) and
// Customize MAME Machines List (TFormArcadeMAMEMachinesCustomize): a checked list of the software
// lists in MAME's hash folder, or of the MAME machines with software lists. The unchecked ones are
// written to arcade\mame_softlist_exclude.txt (their games aren't made or listed) or
// arcade\mame_machines_exclude.txt (the Machines List panel's "disabled" machines).
#pragma once

#include <QDialog>
#include <QIcon>
#include <QStringList>
#include <QVector>
#include <memory>

class QTreeView;
class QStandardItemModel;
class QSortFilterProxyModel;
class QCheckBox;
class QLabel;
class QPushButton;

namespace eld {

class CustomizeListDialog : public QDialog {
    Q_OBJECT
public:
    enum Kind { SoftwareLists, Machines };
    struct Row {
        QString name;         // what goes in the excluded list
        QStringList cells;    // the columns' texts
        QIcon icon;
        bool parent = true;   // Machines: Show Parent Sets Only
        bool gray = false;    // a machine you don't have
    };
    struct Input {
        Kind kind = SoftwareLists;
        QVector<Row> rows;
        QStringList excluded;  // the unchecked ones
        QString emulator;      // version and file, under the title
        QIcon systemIcon, emulatorIcon;
        QString excludeFile;   // where OK writes them
    };
    CustomizeListDialog(const Input& in, QWidget* parent = nullptr);
    ~CustomizeListDialog() override;
    QStringList excluded() const;         // the unchecked rows' names
    // for the tests
    void setChecked(const QString& name, bool on);
    int checkedCount() const;
    int shownCount() const;

private:
    void updateCount();
    void applyFilters();
    void checkAll(bool on);
    void reset();
    void confirm();
    struct Ui;
    std::unique_ptr<Ui> ui_;
    Input in_;
    QStandardItemModel* model_ = nullptr;
    class CustomizeFilter* filter_ = nullptr;
    bool updating_ = false;
};

} // namespace eld
