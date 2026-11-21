// ImagesManagerDialog - MAME Images Manager (TFormImagesManager, uImagesManager.pas; Menu Options
// > Images > MAME Images Manager), for one image category at a time:
//   Scan Missing Images            the games of the games list (as shown) without a picture;
//                                  run one to take its snapshot (Enter, double click, Play);
//   Scan Images of Missing Games   the pictures of games you don't have; Delete Not Used Images
//                                  deletes them;
//   Scan Invalid Images            the files in the category's folders that are no game's picture;
//                                  Delete Invalid Images deletes them.
// The right-click menus have the scan options (clones, BIOS and device sets, missing games,
// arcade / non-arcade machines), Remove Selected (Delete key), a parents / clones filter, Save To
// File, and for files Rename, Move to Folder and View with the associated application.
// Deleted files go to the Recycle Bin. Settings in el_extras.ini [ImagesManager], as the original.
//
// Added here: the pictures inside the category's zip files (snap.zip ...) are listed like the
// others. Deleting, renaming or moving them is collected and written at once (Apply Zip Changes,
// or when the window closes): a new zip is written and replaces the old one only once it's
// checked; the removed pictures go to the Recycle Bin. Scan Invalid Images also lists the files
// that aren't PNG or JPEG pictures inside. Scan All Categories counts every category's pictures,
// missing pictures, pictures of missing games and invalid files; double click a count to list it.
#pragma once

#include "ElDatabase.h"
#include "ImageFinder.h"
#include "ImageIndex.h"
#include "ZipArchive.h"

#include <QDialog>
#include <QIcon>
#include <QSet>
#include <functional>
#include <memory>

namespace Ui { class FormImagesManager; }
class QStandardItemModel;
class QStandardItem;
class QAction;
class QMenu;
class QPushButton;
namespace eld { class ElButton; }
class QTemporaryDir;
namespace eld { class ProgressWindow; }

namespace eld {

class ImagesManagerDialog : public QDialog {
    Q_OBJECT
public:
    struct Setup {
        QList<MameGame> shown;                  // the games list as shown (filters)
        QList<MameGame> all;                    // every game of the list, software list games too (Scan Invalid Images)
        const ImageFinder* finder = nullptr;
        int category = 1;
        QString system;                         // "MAME"
        std::function<QIcon(int category)> categoryIcon;
        std::function<QIcon(const MameGame&)> gameIcon;
        std::function<bool(const QString& name)> play; // selects the game in the main list and runs it
        QString extrasIni;
        QString excludeFile;                    // arcade\el_mamu_exclude.ini: icons not listed as invalid
        QString blankIcon;                      // resources\blank_icon.ico (Create / Edit Icon)
    };
    ImagesManagerDialog(const Setup& setup, QWidget* parent = nullptr);
    ~ImagesManagerDialog() override;

    bool changed() const { return changed_; } // files were deleted, renamed or moved

    enum Scan { ScanMissing, ScanImagesOfMissingGames, ScanInvalid, ScanValid };
    void runScan(int category, Scan scan);
    void switchCategory(int category);       // the image category picked: the lists shown follow it
    QStringList listedGames() const;          // the games list's names
    QStringList listedFiles() const;          // the files list: file, or zip|entry
    int pendingZipChanges() const;
    bool applyZipChanges();
    void deleteListed(Scan scan, bool selectedOnly = false); // Delete Not Used / Invalid / Selected Images, no question
    void selectListed(const QList<int>& rows);  // files list rows (as listedFiles())
    bool renameListed(int row, const QString& name);
    struct Summary { int category; int images, valid, missing, ofMissing, invalid; bool folders; };
    void setSearchClones(bool on);            // the scan options
    void setScanSoftware(bool on);

    // Zip Images (added): a category's valid loose images into the zip next to their folder
    // (snap\ -> snap.zip), stored as they are; Unzip Images the other way.
    enum ZipScope { ZipCategory, ZipSelected, ZipAllCategories };
    enum Conflict { AskConflict, ReplaceInZip, KeepZips };
    struct ZipJob {
        int category = 0;
        QString folder, zip;
        QVector<ZipAdd> files, invalid;       // the valid images; the invalid ones (left out unless asked)
        qint64 bytes = 0, invalidBytes = 0;
        int inZip = 0, conflicts = 0;         // the zip's files now; the images already in it
    };
    QList<ZipJob> zipJobs(ZipScope scope, int category, ProgressWindow* win);
    // Without questions (after the window's): false if something failed
    bool runZip(QList<ZipJob> jobs, bool move, bool includeInvalid, bool replace, QStringList* report = nullptr);
    bool runUnzip(int category, bool replace, bool removeZips, QStringList* report = nullptr);
    QList<Summary> scanAll(bool show);        // Scan All Categories (show: the window with the counts)

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void done(int r) override;

private:
    void sevenZipReadOnly(int count);
    enum Mode { None, Missing, NotUsed, Valid };
    void layoutBars();
    void readSettings();
    void writeSettings();
    void setCategory(int c);
    void selectCategory();
    bool include(const MameGame& g, bool forNotUsed, int category = -1) const;
    void scanMissing(bool notUsed, bool fresh = true);
    void scanInvalid(bool fresh = true);
    void scanValid(bool fresh = true);        // Scan Category (added): the games with a valid image
    void zipImages(ZipScope scope);           // the questions, then runZip
    void unzipImages();
    void refreshLists();                      // the lists shown, scanned again (files moved)
    QString unzipFolder(int category, const QString& zip) const;
    QSet<QString> usedNames(int category) const;
    QStringList excluded() const;
    void setExcluded(const QStringList& files);
    void excludeSelected();
    void editExcluded();
    void createEditIcon();
    void sortFiles();
    void showGameColumns(bool images);        // the Images and Where columns (Scan Category)
    // false: canceled
    bool indexOf(int category, bool fresh, bool content, ProgressWindow* win = nullptr, int barValue = 0);
    void zipsChanged();
    void updateLabels();
    void showPreview(const QString& file, const QString& entry = {});
    void missingSelected();
    void filesSelected();
    void playSelected();
    void removeSelectedGames();
    void filterGames(int which);
    void saveGamesList();
    void deleteNotUsed(bool ask = true);
    void deleteInvalid(bool ask = true, bool selectedOnly = false);
    void showSummary(const QList<Summary>& rows);
    void removeSelectedFiles(bool renamedOnly);
    void renameFile();
    void moveFiles();
    void refreshFileInfo(bool all);
    void saveFilesList();
    void openFile();
    QList<QStandardItem*> selectedFiles() const;
    void addFile(const ImageEntry& e);
    QString extractForView(const QString& zip, const QString& entry);
    bool doRename(QStandardItem* it, QString name);
    void restoreColumns();

    std::unique_ptr<Ui::FormImagesManager> ui_;
    Setup s_;
    QStandardItemModel* games_ = nullptr;
    QStandardItemModel* files_ = nullptr;
    QMenu* gamesMenu_ = nullptr;
    QMenu* filesMenu_ = nullptr;
    QAction* deleteSelected_ = nullptr;
    QAction *searchClones_ = nullptr, *scanMissingGames_ = nullptr, *scanBios_ = nullptr, *scanDevices_ = nullptr,
            *scanArcade_ = nullptr, *scanNonArcade_ = nullptr, *scanSoftware_ = nullptr;
    int category_ = 1;
    Mode mode_ = None;
    int filter_ = 0;      // games filter: 0 all, 1 parents, 2 clones
    bool changed_ = false;
    QString previewFile_, previewEntry_;
    QHash<int, ImageIndex> index_;            // per category, while nothing changes
    QSet<int> checked_;                       // ... with each picture checked to be one
    bool quiet_ = false;
    bool invalidScanned_ = false;             // the files list was made for this category
    QHash<QString, ZipEditor> edits_;         // zip file -> its changes not written yet
    ElButton* scanAll_ = nullptr;
    ElButton* scanCategory_ = nullptr;
    ElButton* scanOptions_ = nullptr;
    ElButton* zipButton_ = nullptr;
    ElButton* unzipButton_ = nullptr;
    QAction* zipSelected_ = nullptr;
    QAction *createIcon_ = nullptr, *sortByName_ = nullptr, *sortByDate_ = nullptr;
    QAction *iconsSeparator_ = nullptr, *excludeAdd_ = nullptr, *excludeEdit_ = nullptr;
    bool byDate_ = false;                     // the files list by newest date
    int validImages_ = 0;
    ElButton* applyZips_ = nullptr;
    QWidget* bottomLeft_ = nullptr; // the bottom bar's half under the games list
    std::unique_ptr<QTemporaryDir> viewDir_;  // zipped pictures opened with their application
};

} // namespace eld
