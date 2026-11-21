// MamuIcons - MAMu_ icons (PopupMAMu_Icons): each game's own icon instead of the parent/clone
// one, from the icons folders (icons_directory: MAME's ui.ini, else arcade\sysimagefolders_arcade.ini
// [MAME]; "icons" in MAME's folder by default): <folder>\<name>.ico, <folder>\icons\<name>.ico,
// or <name>.ico in <folder>\icons.zip (GetMAMu_IconFileName, ScanFoldersIcon).
//
// What's looked for, in order: the BIOS's icon (Force Game Bios Icons), the driver's (Force Game
// Driver Icons), the game's (Enable Clone Icons, then its parent's with Enable Parent Icons;
// a parent's with Enable Parent Icons), deviceset.ico for device sets (Enable Device Set Custom
// Icon), the BIOS's, the driver's. Settings: EmuLoaderRL.ini [MAMu_].
#pragma once

#include "ZipArchive.h"

#include <QHash>
#include <QIcon>
#include <QStringList>
#include <memory>
#include <vector>

namespace eld {

struct MameGame;
class WinIni;

class MamuIcons {
public:
    struct Options {
        bool enabled = false, parent = true, clone = true, deviceSet = false;
        bool bios = false, driver = false, forceBios = false, forceDriver = false;
        void load(const WinIni& ini);
        void save(WinIni& ini) const;
    };
    void setOptions(const Options& o);
    const Options& options() const { return o_; }
    void setFolders(const QStringList& folders); // icons_directory, resolved
    const QStringList& folders() const { return folders_; }
    // the game's MAMu_ icon; a null icon when there's none (the list's own icon then)
    QIcon icon(const MameGame& g);
    void reload();                                // Reload All Icons: read the files again
    void release() { zips_.clear(); preloaded_.clear(); found_.clear(); icons_.clear(); } // icons.zip closed (setFolders reads it again)
    int zippedIcons() const;
    // The file the icon would come from ("" when none): a path, or "<zip>|<entry>"
    QString source(const MameGame& g);

private:
    QString find(const QString& name);           // a loose .ico or a zip entry, "" if none
    QIcon load(const QString& where);
    Options o_;
    QStringList folders_;
    std::vector<std::unique_ptr<ZipArchive>> zips_;
    // A slow icons.7z (one big solid block) read in full once: "<archive>|<icon, lowercase>" -> its bytes
    QHash<QString, QByteArray> preloaded_;
    QHash<QString, QString> found_;               // name -> where ("" = none)
    QHash<QString, QIcon> icons_;                 // where -> icon
};

} // namespace eld
