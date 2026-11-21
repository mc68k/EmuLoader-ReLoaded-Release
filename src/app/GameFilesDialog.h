// GameFilesDialog - Delete Selected Games Files / Copy Selected Games Files to Folder / Move
// Selected Games Files to Folder (games list menu; the original's TFormDeleteGameFiles for one
// game and TFormDeleteMultipleGamesFiles for several, with TFormCopyMoveGameFiles for the work).
//
// One window for one game or many: each game with its files (ROM set, CHDs, config/NVRAM),
// each with a check box. The file type boxes check or uncheck a type everywhere, as the
// original's "Auto-Check Arcade File Types". Copy and move take a destination folder, Overwrite
// Files and Add System Folder. Settings are kept in el_extras.ini [DeleteFiles] and
// [CopyMoveFiles], the original's keys.
//
// Differences: deleted files go to the Recycle Bin (the original deletes them for good); a
// copied or moved CHD goes into a folder named after its game, where MAME looks for it (the
// original put them all in one "chd_files" folder).
#pragma once

#include "GameFiles.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace eld {

class GameFilesDialog : public QDialog {
    Q_OBJECT
public:
    struct Game {
        QString name, title;
        QList<GameFile> files;
    };
    GameFilesDialog(FileAction action, const QString& caption, const QList<Game>& games, const QString& extrasIni,
                    const QString& systemFolder, QWidget* parent = nullptr);
    bool changedFiles() const { return changed_; } // something was deleted or moved: rescan

private:
    void autoCheck(GameFile::Kind kind, bool on);
    void updateTotals();
    QList<GameFile> checkedFiles() const;
    void process();
    void readSettings();
    void writeSettings();

    FileAction action_;
    QList<Game> games_;
    QString extrasIni_, systemFolder_;
    QTreeWidget* tree_ = nullptr;
    QCheckBox *rom_ = nullptr, *chd_ = nullptr, *cfg_ = nullptr;
    QLineEdit* dest_ = nullptr;
    QCheckBox *overwrite_ = nullptr, *addSystem_ = nullptr;
    QLabel* totals_ = nullptr;
    QPushButton* go_ = nullptr;
    bool changed_ = false;
    bool filling_ = false;
};

} // namespace eld
