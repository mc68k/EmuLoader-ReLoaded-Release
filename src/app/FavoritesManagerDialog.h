// FavoritesManagerDialog - Favorites Manager (TFormFavoritesManager, uFavoritesManager.pas;
// Favorites button's right-click menu > More...): the favorites profiles in <data>\favorites folder
// (one .txt file each). New, Clear Games, Cleanse (remove entries for games that aren't in
// the games list, uFavoritesManagerCleanseProfile), Replicate, Delete, edit a title (F2) or a
// file name (F3), and pick the active profile ([Preferences] FavoriteFile).
//
// While the favorites list is shown in the main window only another profile can be picked,
// as in the original. Window size and columns are kept in el_extras.ini [FavoritesManager].
#pragma once

#include "Favorites.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormFavoritesManager; }
class QStandardItemModel;
class QStandardItem;

namespace eld {

class FavoritesManagerDialog : public QDialog {
    Q_OBJECT
public:
    struct Setup {
        QString folder;                 // <data>/favorites
        QString activeFile;             // [Preferences] FavoriteFile
        QString extrasIni;              // el_extras.ini
        bool selectOnly = false;        // the favorites list is on: only pick a profile
        QIcon activeIcon, settingsIcon; // favorite_game.ico, settings.ico
        // Cleanse: true when a profile line names a game that's still there (or one this
        // version can't judge: other systems, software lists), false for an "impurity".
        std::function<bool(const QString& line)> isValid;
        std::function<QString(const QString& line)> systemOf; // "MAME", "HBMAME", ... for the list
    };
    FavoritesManagerDialog(const Setup& setup, QWidget* parent = nullptr);
    ~FavoritesManagerDialog() override;

    QString activeFile() const { return active_; }
    bool activeChanged() const { return activeContentsChanged_; } // the active profile's games changed

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void done(int r) override;

private:
    void load(const QString& select = {});
    void fillRow(int row, const FavoriteProfiles::Profile& p);
    QString fileAt(int row) const;
    int selectedRow() const;
    bool needSelection(const QString& title); // "Select a list first." under that title
    void createProfile(bool copy);
    void clearGames();
    void cleanse();
    void deleteProfile();
    void setActive();
    void edit(int column);
    void onEdited(QStandardItem* item);
    void readSettings();
    void writeSettings();
    void markActive();

    std::unique_ptr<Ui::FormFavoritesManager> ui_;
    Setup s_;
    QStandardItemModel* model_ = nullptr;
    QString active_;
    bool activeContentsChanged_ = false;
    bool filling_ = false;
};

} // namespace eld
