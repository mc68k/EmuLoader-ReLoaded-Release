// LastPlayedDialog - Last Played Games [Top 25] (TFormLastPlayedGames, uLastPlayedGames.pas; the
// Last Played tool bar button): the systems that have play statistics at the top, the 25 most
// recently played games of the chosen one below (arcade\played_games\mame.txt / hbmame.txt).
// Select Game selects it in the main games list.
//
// For later, when non-MAME systems come back (consoles/computers through other emulators):
// - The window is already data-driven: add one System per system with a played file; no other
//   change is needed here except letting the Systems strip grow to more rows (the original has
//   room for 4 rows of 19 icons, ResizeForm).
// - Their plays are in one file per system (GetCustomGamePlayedFile); a line is
//   "<file>Game_filename/> MediaType_ID  ' '  TimesPlayed;PlayedDate;TotalPlayTime", the file
//   name encoded (DecodeUnicodeStr). Read in TFormLastPlayedGames.AddGamesMRU and
//   GetPlayedGameInfoIniCustom (uLastPlayedGames.pas, uMain.pas).
// - Show them when those emulators are enabled (the same switch as Enable Console and Computer
//   Support), rather than a separate "Hide non-MAME systems" preference; add one only if the
//   emulators can be on while their games are kept out of this window.
// - Write the reader together with the systems' port, not before: system numbering, media types
//   and what Select Game matches against depend on it.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormLastPlayedGames; }
class QStandardItemModel;
class QListWidget;

namespace eld {

class LastPlayedDialog : public QDialog {
    Q_OBJECT
public:
    struct Game {
        QString name;          // the line's name: "pacman", or "list\name" for software
        QString title;         // from the games list when it's there, the name otherwise
        QString softwareTitle; // software list games
        QString lastPlayed, playtime;
        int played = 0;
        bool favorite = false;
        QIcon icon;
    };
    struct System {
        SystemId id;
        QString title;         // "MAME", "HBMAME"
        QIcon icon;
        QIcon star;            // favorite_game.ico: shown before a favorite's icon
        QList<Game> games;     // the top 25, the last played first
    };
    // select: selects the game in the main list; false when it isn't there (filters, other system).
    LastPlayedDialog(const QList<System>& systems, SystemId current,
                     std::function<bool(SystemId, const QString&)> select, QWidget* parent = nullptr);
    ~LastPlayedDialog() override;

    static constexpr int kTop = 25;

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void showSystem(int row);
    void selectGame(bool close);
    void layoutWindow();

    std::unique_ptr<Ui::FormLastPlayedGames> ui_;
    QList<System> systems_;
    std::function<bool(SystemId, const QString&)> select_;
    QListWidget* systemsView_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    int shown_ = -1;
};

} // namespace eld
