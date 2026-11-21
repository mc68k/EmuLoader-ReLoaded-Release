// ScanResultsDialog - the games list's menu > View Missing ROMs/CHDs > All Games / Selected Game
// (TFormArcadeScanGamesResults): what the last Full Scan found wrong, a set at a time, each with
// its files: name, the CRC32 and SHA-1 MAME expected (for a wrong checksum), size, the BIOS or
// device it belongs to, and the problem. The original's filters: arcade or software list games,
// CHDs Only, the sets with BIOS ROMs and device ROMs; plus Sets Not Found (sets with no file at
// all, off at first) and the search box. Double-click (or Enter) selects the game in the games
// list; the right-click menu also saves the list to a text file.
#pragma once

#include "ElDatabase.h"
#include "Frontend.h"
#include "RomAudit.h"

#include <QDialog>
#include <functional>
#include <memory>

namespace Ui { class FormArcadeScanGamesResults; }
class QAction;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;

namespace eld {

class ScanResultsDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        Frontend* fe = nullptr;
        RomAudit::Results results;
        QString only;                                    // one set (Selected Game), else all
        bool showNotFound = false;                       // Sets Not Found checked (a scan's own results)
        bool showSamples = true;                         // the audio samples' results (Scan Games' Audio Samples)
        QString emulatorVersion, listVersion;            // the top bar's
        std::function<const MameGame*(const QString& key)> game; // "pacman", "nes:smb"
        std::function<QIcon(const MameGame&)> icon;
        std::function<bool(const QString& key)> select;  // in the games list; false when it's hidden
    };
    explicit ScanResultsDialog(const Input& in, QWidget* parent = nullptr);
    ~ScanResultsDialog() override;

    QTreeWidget* tree() const { return tree_; }
    QStringList shown() const;                 // the sets listed, in order
    void setNotFoundShown(bool on);
    void setChdsOnly(bool on);
    void setKindFilter(int index);             // 0 all, 1 arcade, 2 software list games
    void search(const QString& text);
    bool saveList(const QString& file) const;
    QString totalText() const;
    QString commandOf(int top, int child) const; // a file row's chdman command ("" when none)

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill();
    void layoutTop(bool software);
    void filter();
    void selectCurrent();
    void saveListAsk();
    std::unique_ptr<Ui::FormArcadeScanGamesResults> ui_;
    Input in_;
    QTreeWidget* tree_ = nullptr;
    QCheckBox* notFound_ = nullptr;
    QMenu* menu_ = nullptr;
    QAction* copyCommand_ = nullptr;
    QString searchText_;
};

} // namespace eld
