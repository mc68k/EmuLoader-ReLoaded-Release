// ImageFinder - EmuLoader's image categories for MAME (TFormMain.LoadScreenshot,
// ReadArcadeImageCategories): which folders each category uses and how an image is found.
//
// Folders: MAME's ui.ini (titles_directory, flyers_directory, ...; next to EmuLoader first,
// "linux mode", then next to MAME) and mame.ini for snapshots (snapshot_directory). Without a
// ui.ini: arcade\sysimagefolders_arcade.ini [MAME]. Each setting can list several folders.
//
// Search, for image number n of a game (1 = the first):
//   <folder>\<name>.png, <folder>\<category folder>\<name>.png, then .jpg (not for the
//   categories MAME only stores as PNG); n > 1: <name>0000.png, <name>0001.png ... (EmuLoader
//   naming) and, for snapshots, MAME's own <name>\0000.png, <name>\0001.png ...;
//   then the parent's images (Display Parent Images), then inside <category>.zip
//   (snap.zip, flyers.zip ...) in the folder or next to it.
// Software list games are named <list>/<name> (MameGame::imageName): <folder>\nes\smb.png,
// nes\smb0000.png, nes\smb\0000.png; and in zips: <category>.zip (nes/smb.png), <folder>\nes.zip
// and <folder>\nes\nes.zip (smb.png), <folder>\snap_sl.zip (nes/smb.png; titles_sl.zip ...).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

namespace eld {

struct ImageCategory {
    const char* icon;     // resources\main_icons\<icon>.ico
    const char* section;  // image_category.ini key prefix
    const char* missing;  // resources\images\img_missing\<file> when there is no image
    const char* folder;   // MAME's default folder name (also <folder>.zip)
    const char* option;   // ui.ini / mame.ini option
    const char* title;
};

class ImageFinder {
public:
    static constexpr int Count = 17;
    // The MAMu_ icons (the Images Manager's Icons category): <folder>\<name>.ico and icons.zip, in
    // the folders of MAME's ui.ini icons_directory (setIconFolders). Not one of the image panels'.
    static constexpr int Icons = Count;
    static constexpr int CountWithIcons = Count + 1;
    static constexpr int MaxImages = 99; // MaxImagePerCategory
    static const ImageCategory& category(int i);
    static QString title(int i); // the category's title in the user's language
    static bool pngOnly(int category) { return category <= 1 || (category >= 8 && category < Count); } // ImagesPNGOnly

    // dataDir: EmuLoader's data folder (its ui.ini); userDir: this version's files
    // (arcade\sysimagefolders_arcade.ini); mameFolder: where MAME is; mameIni: its mame.ini;
    // section: the system's section of sysimagefolders_arcade.ini ("MAME", "HBMAME").
    void setup(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni,
               const QString& section = QStringLiteral("MAME"));

    // The folder settings as written (ReadArcadeImageCategories / WriteArcadeImageCategories,
    // Category Settings): ui.ini (next to EmuLoader, else next to MAME) for every category but
    // the snapshots, which are mame.ini's snapshot_directory; without a ui.ini,
    // arcade\sysimagefolders_arcade.ini [<section>]. "" means MAME's default folder.
    static QStringList readSettings(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni,
                                    const QString& section);
    static bool writeSettings(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni,
                              const QString& section, const QStringList& values);
    QStringList folders(int category) const { return folders_.value(category); }
    void setIconFolders(const QStringList& folders) { folders_[Icons] = folders; }

    struct Found {
        bool ok = false;
        QString file;      // the image file, or the zip archive
        QString entry;     // the file inside the zip ("" when not zipped)
        QString type;      // PNG / JPG
        bool zipped() const { return !entry.isEmpty(); }
    };
    // Image number `index` (1-based) of `name`, then of `parent` when given.
    Found find(int category, const QString& name, const QString& parent, int index) const;
    // The image's bytes; a zip stays open for the next one (ZipArchive::shared). Any thread.
    // keepGoing: false when it's not wanted any more (another game selected): empty then
    static QByteArray read(const Found& f, const std::function<bool()>& keepGoing = {});
    static int readsStarted(); // the reads that went ahead (still wanted when their turn came)

    // The game's images outside zip files, all of them (index 1..99, PNG and JPG): what Delete
    // Images of Clone Games deletes and Rename to Parent renames (uImagesDeleteClones).
    QStringList gameFiles(int category, const QString& name) const;
    // ScanFiles (Images Manager > Scan Invalid Images): the PNG (and JPG) files in the category's
    // folders and their subfolders that are no picture of any of `names`.
    QStringList unusedFiles(int category, const QStringList& names) const;
    // The same file for another game: <folder>\pacman0001.png -> <folder>\puckman0001.png,
    // <snap>\pacman\0000.png -> <snap>\puckman\0000.png. "" when `file` isn't one of `from`'s.
    static QString renamedFor(const QString& file, QString from, QString to);
    // The category's zip files that exist: <folder>\<category>.zip, <folder>.zip, and the software
    // lists' (<folder>\<list>.zip, <folder>\<list>\<list>.zip, <folder>\<category>_sl.zip)
    QStringList zipFiles(int category) const;
    // The software lists made (their names): where their games' pictures may be
    void setSoftwareLists(const QStringList& lists);
    bool isSoftwareList(const QString& name) const { return softwareLists_.contains(name.toLower()); }
    // What a zip's entry names are relative to: "nes/" for nes.zip, "" for snap.zip and snap_sl.zip
    QString zipPrefix(const QString& zip) const;
    static QStringList withSevenZip(const QStringList& zips); // snap.zip -> snap.zip, snap.7z
    // The picture extensions the category uses: png (and jpg)
    static QStringList extensions(int category) {
        return category == Icons ? QStringList{"ico"} : pngOnly(category) ? QStringList{"png"} : QStringList{"png", "jpg"};
    }
    // Zip files changed: their file lists are read again
    void forgetZips() const;

private:
    Found findIn(int category, const QString& name, int index) const;
    const QSet<QString>& zipEntries(const QString& zip) const;
    QHash<int, QStringList> folders_;
    mutable QHash<QString, QSet<QString>> zipCache_; // zip -> lower-case entry names
    QSet<QString> softwareLists_;                     // lower case
};

} // namespace eld
