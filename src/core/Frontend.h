// Frontend - one EmuLoader installation: its data folder, this version's settings and the
// MAME-family emulators set up in it (MAME, HBMAME).
//
// Two folders:
//   the data folder (dataDir)   EmuLoader's folder, shared with the original and only read:
//                               resources\, docs\, arcade\filters\ (catver.ini ...), lang\;
//                               relative paths in the settings (MAME, pictures) start there.
//   EmuLoaderRL\ in it (userDir) this version's own files, with the original's layout:
//                               EmuLoaderRL.ini (the original's EmuLoader.ini), arcade\emulators.ini,
//                               arcade\games\ (the games lists), ini_files\, favorites\,
//                               el_extras.ini, arcade\played_games\, arcade\custom_params\ ...
// The first time, the user's own data is copied from the original's files (importOriginal); the
// games lists are made again from MAME. After that, the original's files are never written to.
// MAME's own files (mame.ini, ui.ini, the ini folder) are shared, as they belong to MAME.
#pragma once

#include "ElDatabase.h"
#include "MameIni.h"
#include "WinIni.h"

#include <QString>
#include <QStringList>

namespace eld {

enum class SystemId { MAME = 1, HBMAME = 5 }; // ids as in EmuLoader (uCommon.pas)

// One emulator as Arcade Emulators Setup writes it to arcade\emulators.ini.
struct EmulatorInfo {
    QString file;          // emu_FileName (absolute)
    QString version;       // emu_VersionInfo: "MAME v0.289 (Aug 03 2026)"
    QString build;         // emu_Build: "0.289"
    qint32 dateTime = -1;  // emu_DateTime: the file's date, DOS format (Delphi FileAge)
    bool isSet() const { return !file.isEmpty(); }
};

class Frontend {
public:
    bool open(const QString& dataDir);            // loads the settings (imports the original's the first time)
    const QString& dataDir() const { return dataDir_; }
    const QString& userDir() const { return userDir_; }   // <data>/EmuLoaderRL/
    QString userFile(const QString& relative) const;       // <data>/EmuLoaderRL/<relative>
    WinIni& ini() { return ini_; }                // EmuLoaderRL.ini (preferences, tool bar...)
    // The first start's import from the original EmuLoader: what was copied and what couldn't be
    // ("" when there was nothing to import, or it's not the first start).
    bool imported() const { return imported_; }
    const QStringList& importedItems() const { return importedItems_; }
    const QStringList& importFailures() const { return importFailures_; }
    // Imports from an EmuLoader in another folder (the first start's question, when this folder
    // has none); false when the folder has no EmuLoader.ini or the settings can't be saved.
    bool importFrom(const QString& folder);
    // Also an earlier copy of this version: <folder>\EmuLoaderRL\ (or that folder), copied whole.
    static bool canImportFrom(const QString& folder);
    bool importedFromCopy() const { return importedFromCopy_; }
    bool saveIni() { return ini_.save(iniPath()) && (!emusLoaded_ || emus_.save(emulatorsPath())); }
    QString iniPath() const;
    QString emulatorsPath() const;                // arcade\emulators.ini (emulator paths/versions)
    QString iniFilesFolder() const;               // <user>/ini_files/ (custom fonts, ...)

    static QString section(SystemId s) { return s == SystemId::MAME ? "MAME" : "HBMAME"; }
    static QString fileBase(SystemId s) { return s == SystemId::MAME ? "mame" : "hbmame"; }

    // ---- emulator (arcade\emulators.ini [MAME] emu_FileName / emu_Build / emu_VersionInfo,
    //      written by EmuLoader's Arcade Emulators Setup)
    QString emulatorFile(SystemId s) const;       // absolute path, empty if not set
    EmulatorInfo emulator(SystemId s) const;
    void setEmulator(SystemId s, const EmulatorInfo& e); // an empty file deletes the keys (VerifyEmulator)
    // (The original's AlterMAME 1 and 2, [MAME] emu_AlterMAMEFile ..., aren't used; their keys are
    // left in the file as they are.)
    // GetEmulatorDefaultDescription: the emulator's details from its file and "-version" answer
    // ("0.289 (mame0289)"); version empty when there is no answer.
    static EmulatorInfo describe(SystemId s, const QString& exe, const QString& versionAnswer);
    static QString description(SystemId s, const QString& exe); // "MAME", "HBMAME: HomeBrew MAME"
    static qint32 fileDosDate(const QString& file);
    int mameBuild(SystemId s) const;              // 270 for "0.270", -1 = unknown/latest
    QString emulatorVersion(SystemId s) const;    // emu_VersionInfo
    QString mameFolder(SystemId s) const;
    QString mameIniPath(SystemId s) const;        // <mame folder>/mame.ini (or hbmame.ini)
    QString iniDir(SystemId s) const;             // per-machine ini folder (from inipath)
    QStringList romFolders(SystemId s) const;     // rompath, resolved
    QStringList sampleFolders(SystemId s) const;  // samplepath, resolved
    // Game manuals (PDF): arcade\sysimagefolders_arcade.ini [MAME] manuals_directory ("manuals"),
    // relative to MAME's folder, for both systems (MAMEFolderManualPDF).
    QString manualsSetting() const;
    // MAMu_ icons: icons_directory of MAME's ui.ini (next to EmuLoader, else next to MAME), else
    // arcade\sysimagefolders_arcade.ini [MAME]; "icons" by default; several separated by ";".
    QString iconsSetting() const;
    void setIconsSetting(const QString& folders);
    QStringList iconFolders() const;
    QString uiIniPath() const;                    // MAME's ui.ini in use ("" if none)
    void setManualsSetting(const QString& folder);
    QString manualsFolder() const;
    // OpenGameManualPDF: <folder>\<name>.pdf, <folder>\manuals\<name>.pdf, then the parent's.
    QString gameManual(const QString& name, const QString& cloneOf) const;

    // ---- database
    QString gamesFolder() const;                  // <user>/arcade/games/
    QString databaseFile(SystemId s) const { return gamesFolder() + fileBase(s) + ".el"; }
    QString statusFile(SystemId s) const { return gamesFolder() + fileBase(s) + ".elstatus"; }
    QString softwareListsFile(SystemId s) const { return gamesFolder() + fileBase(s) + ".elsoftlist"; }
    QString gamesFiltersFile() const { return iniFilesFolder() + "games_filters.ini"; }
    QString samplesFile(SystemId s) const { return gamesFolder() + fileBase(s) + ".elsamples"; }
    QString biosSetsFile(SystemId s) const { return gamesFolder() + fileBase(s) + "_biossets.ini"; }
    QString resourcesFolder() const;
    // resources\main_icons\<file>, or main_icons\alternate\<file> when there's one there and Menu
    // Options > Use Alternate Frontend Icons is on ([Preferences] UseAlternateFrontendIcons).
    QString mainIcon(const QString& file) const;              // <data>/resources/ (icons, images)

    ChainMachine chainMachine(const MameGame& g) const;

private:
    void migrateSection(const QString& from, const QString& to);
    void importOriginal(const QString& folder);
    void load();
    static QString earlierCopy(const QString& folder);
    QString dataDir_, userDir_;
    bool imported_ = false, importedFromCopy_ = false;
    QStringList importedItems_, importFailures_;
    WinIni ini_;
    WinIni emus_;
    bool emusLoaded_ = false;
};

} // namespace eld
