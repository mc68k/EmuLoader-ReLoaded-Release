// MultiSlotDialog - Multi-Slot Machines (TFormArcadeMultiSlotGames, uArcadeMultiSlotGames.pas):
// the selected games, one per cartridge slot, to put in the order they go in (Move Up / Move
// Down, Ctrl+Up / Ctrl+Down) or take out (Remove, Delete). For the Neo-Geo MVS, the board to run
// them with (Change): the games past its slots are grayed and stay out. See MultiSlot.h.
#pragma once

#include "ElDatabase.h"
#include "MultiSlot.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormArcadeMultiSlotGames; }
class QStandardItemModel;

namespace eld {

class MultiSlotDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        QString machine;                     // "neogeo", "stv", "megatech", "playch10"
        QVector<MameGame> games;             // in the order they were selected
        QVector<MultiSlot::Board> boards;    // the Neo-Geo's (none: the panel isn't shown)
        QString board;                       // the board last used
        std::function<QIcon(const MameGame&)> icon;
        std::function<QString(const MameGame&)> players; // the games list's Players text
    };
    MultiSlotDialog(const Input& in, QWidget* parent = nullptr);
    ~MultiSlotDialog() override;

    QStringList games() const;               // the games that go in the slots, in order
    QString board() const { return board_; } // the Neo-Geo board chosen ("" for the others)
    int slotCount() const;                   // how many slots the machine (or board) has

    QStringList listed() const;              // every game listed, in order (grayed ones too)
    void selectRow(int row);
    void moveSelected(bool up);
    void removeSelected();
    void showBoards();                       // Change
    void chooseBoard(const QString& name);   // a board picked in the panel, then OK
    bool boardsShown() const;

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void updateSlots();
    void setBoard(const QString& name);
    void hideBoards(bool use);
    std::unique_ptr<Ui::FormArcadeMultiSlotGames> ui_;
    Input in_;
    QStandardItemModel* games_ = nullptr;
    QStandardItemModel* boards_ = nullptr;
    QString board_;
};

} // namespace eld
