// ExportGamesDialog - Menu Options > Games List > Export MAME and Arcade Games List
// (TFormArcadeExportGamesList): writes the games list to a text file, in one of three formats:
//   Full Format       the game info chosen under Game Info To Export, in that order, as
//                     "Title - Year - ..." lines, or in Microsoft Excel's CSV format; with the
//                     titles as the first line
//   Short Format      "gamename=system" lines (as favorites files are)
//   MAME Content Manager Plus  set names only
// each for all the games of the list, the ones shown (the filters and search), or the selected
// ones. The choices are kept in el_extras.ini [ExportArcadeGames], the original's keys.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QStringList>
#include <memory>

class QListWidget;
class QTreeView;
class QTreeWidget;
namespace Ui { class FormArcadeExportGamesList; }

namespace eld {

class GamesModel;
class GamesFilter;

class ExportGamesDialog : public QDialog {
    Q_OBJECT
public:
    struct Source {
        GamesModel* model = nullptr;
        GamesFilter* filter = nullptr;
        QTreeView* list = nullptr; // the games list: its selection and its visible columns
        SystemId system = SystemId::MAME;
    };
    // Mode: the ExportList item (its ImageIndex in the original)
    enum Mode { FullAll = 0, FullShown = 1, ShortAll = 2, ShortShown = 3, FullSelected = 4, ShortSelected = 5,
                McmAll = 6, McmShown = 7, McmSelected = 8 };

    ExportGamesDialog(const Source& src, const QString& extrasIni, QWidget* parent = nullptr);
    ~ExportGamesDialog() override;

    // The file's lines for the mode chosen, as GenerateFile makes them (no file is written).
    QStringList lines() const;
    // Writes them to a file (CRLF line ends; UTF-8, with a BOM in Excel format).
    bool write(const QString& file, QString* error = nullptr) const;
    void layoutWindow();
    void setMode(Mode m);
    // The file to write to, instead of asking.
    void setFileForTest(const QString& f) { testFile_ = f; }

private:
    void readSettings(bool columnsOnly = false);
    void writeSettings() const;
    void defaultColumns();
    bool generate();       // asks where, then writes; false when nothing was written
    void moveColumn(int by);
    Mode mode() const;
    void help();
    void excelHelp();

    std::unique_ptr<Ui::FormArcadeExportGamesList> ui_;
    Source src_;
    QString extrasIni_, testFile_;
    QTreeWidget* modes_;
    QListWidget* info_;
};

} // namespace eld
