#include "Frontend.h"
#include "TextFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

namespace eld {

// The user's own data in the original EmuLoader's files, copied the first time (the original's
// name, then this version's when it differs). Games lists, BIOS lists, software lists and memory
// card lists aren't copied: they're made again from MAME (Create Arcade Games List), so that
// nothing wrong in them comes along.
static const char* const kImport[][2] = {
    {"EmuLoader.ini", "EmuLoaderRL.ini"},  // settings, tool bar, filters, last selected game ...
    {"arcade/emulators.ini", nullptr},      // MAME and HBMAME (the original's AlterMAME keys are left as they are)
    {"ini_files", nullptr},                 // columns, image categories, layouts, light mode, fonts, video preview ...
    {"favorites", nullptr},                 // favorites profiles
    {"el_extras.ini", nullptr},             // window settings of the tools (Images Manager ...)
    {"arcade/played_games", nullptr},       // Last Played, play counts, playtime
    {"arcade/custom_params", nullptr},      // Custom Parameters
    {"arcade/memcard_lastused", nullptr},   // Run Game (Extra) memory cards
    {"arcade/sysimagefolders_arcade.ini", nullptr}, // image folders, when MAME has no ui.ini
    {"arcade/mame_softlist_exclude.txt", nullptr},  // software lists left out (Customize Software List)
    {"arcade/hbmame_softlist_exclude.txt", nullptr},
    {"arcade/mame_machines_usercustom.ini", nullptr}, // the machine chosen to run each software list
    {"arcade/hbmame_machines_usercustom.ini", nullptr},
    {"arcade/mame_machines_exclude.txt", nullptr},   // the Machines List panel's disabled machines
    {"arcade/hbmame_machines_exclude.txt", nullptr},
};

// Copies a file or a folder (what's already there stays); false for each file that couldn't be.
static void copyItem(const QString& from, const QString& to, QStringList& failures) {
    QFileInfo fi(from);
    if (fi.isDir()) {
        if (!QDir().mkpath(to)) { failures << QDir::toNativeSeparators(from); return; }
        for (const QFileInfo& c : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
            copyItem(c.absoluteFilePath(), QDir(to).filePath(c.fileName()), failures);
        return;
    }
    if (QFileInfo::exists(to)) return;
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (!QFile::copy(from, to)) failures << QDir::toNativeSeparators(from);
}

void Frontend::importOriginal(const QString& folder) {
    for (const auto& item : kImport) {
        QString from = QDir(folder).filePath(item[0]);
        if (!QFileInfo::exists(from)) continue;
        int before = int(importFailures_.size());
        copyItem(from, userFile(item[1] ? item[1] : item[0]), importFailures_);
        if (importFailures_.size() == before) importedItems_ << QDir::toNativeSeparators(item[0]);
        imported_ = true;
    }
}

QString Frontend::userFile(const QString& relative) const { return QDir(userDir_).filePath(relative); }

bool Frontend::open(const QString& dataDir) {
    dataDir_ = QDir(dataDir).absolutePath();
    userDir_ = QDir(dataDir_).filePath("EmuLoaderRL");
    imported_ = false;
    importedItems_.clear();
    importFailures_.clear();
    if (!QFileInfo::exists(iniPath())) {
        QDir().mkpath(userDir_);
        importOriginal(dataDir_); // the first start: the user's data from the original EmuLoader
    }
    load();
    return QFileInfo(dataDir_).isDir();
}

// The first start's question, for an EmuLoader in another folder: its data is copied as above
// (the settings file replaces the one just made; the language just chosen is kept), and the
// emulators' relative paths are made absolute, as they were relative to that folder.
// An earlier copy of this version (its EmuLoaderRL folder, or that folder itself) is copied whole,
// games lists too, as they're this version's own; the original's files in that folder are then
// left out, as they're older.
QString Frontend::earlierCopy(const QString& folder) {
    QDir d(folder);
    if (QFileInfo::exists(d.filePath("EmuLoaderRL/EmuLoaderRL.ini"))) return d.filePath("EmuLoaderRL");
    if (QFileInfo::exists(d.filePath("EmuLoaderRL.ini"))) return d.absolutePath();
    return {};
}

bool Frontend::canImportFrom(const QString& folder) {
    return !earlierCopy(folder).isEmpty() || QFileInfo::exists(QDir(folder).filePath("EmuLoader.ini"));
}

bool Frontend::importFrom(const QString& folder) {
    QString from = QDir(folder).absolutePath();
    if (!canImportFrom(from)) return false;
    const QString lang = ini_.readString("EmuLoaderRL", "Language");
    QFile::remove(iniPath());
    QFile::remove(emulatorsPath());
    importedItems_.clear();
    importFailures_.clear();
    QString copy = earlierCopy(from);
    importedFromCopy_ = !copy.isEmpty();
    if (importedFromCopy_) {
        copyItem(copy, userDir_, importFailures_);
        imported_ = true;
        from = QFileInfo(copy).dir().absolutePath(); // its data folder: relative paths start there
    } else {
        importOriginal(from);
    }
    load();
    for (SystemId s : {SystemId::MAME, SystemId::HBMAME}) {
        QString f = emus_.readString(section(s), "emu_FileName");
        if (!f.isEmpty() && QDir::isRelativePath(QString(f).replace('\\', '/')))
            emus_.writeString(section(s), "emu_FileName", QDir::toNativeSeparators(resolvePath(from, f)));
    }
    if (!lang.isEmpty()) ini_.writeString("EmuLoaderRL", "Language", lang);
    return saveIni();
}

void Frontend::load() {
    if (!ini_.load(iniPath())) ini_ = WinIni{};
    emus_ = WinIni{};
    emus_.load(emulatorsPath()); // missing on a fresh install; created on first save
    emusLoaded_ = true;
    // Earlier builds of this port wrote the emulator to EmuLoader.ini; move it over.
    for (SystemId s : {SystemId::MAME, SystemId::HBMAME})
        if (emus_.readString(section(s), "emu_FileName").isEmpty() && !ini_.readString(section(s), "emu_FileName").isEmpty())
            for (const char* k : {"emu_FileName", "emu_VersionInfo", "emu_Build", "emu_DateTime"}) {
                emus_.writeString(section(s), k, ini_.readString(section(s), k));
                ini_.deleteKey(section(s), k);
            }
    // This port's own settings were in [EmuLoaderQt] before it became EmuLoader ReLoaded: move them
    // to [EmuLoaderRL] (a key already there wins). Saved with the other settings.
    migrateSection("EmuLoaderQt", "EmuLoaderRL");
}

void Frontend::migrateSection(const QString& from, const QString& to) {
    if (!ini_.sectionExists(from)) return;
    const QStringList existing = ini_.keys(to);
    for (const QString& k : ini_.keys(from)) {
        bool there = false;
        for (const QString& e : existing) there |= e.compare(k, Qt::CaseInsensitive) == 0;
        if (!there) ini_.writeString(to, k, ini_.readString(from, k));
    }
    ini_.eraseSection(from);
}

QString Frontend::iniPath() const { return userFile("EmuLoaderRL.ini"); }
QString Frontend::emulatorsPath() const { return userFile("arcade/emulators.ini"); }
QString Frontend::iniFilesFolder() const { return userFile("ini_files/"); }

QString Frontend::emulatorVersion(SystemId s) const { return emus_.readString(section(s), "emu_VersionInfo"); }

QString Frontend::emulatorFile(SystemId s) const {
    QString f = emus_.readString(section(s), "emu_FileName");
    return f.isEmpty() ? QString() : resolvePath(dataDir_, f);
}

EmulatorInfo Frontend::emulator(SystemId s) const {
    EmulatorInfo e;
    e.file = emulatorFile(s);
    e.version = emus_.readString(section(s), "emu_VersionInfo");
    e.build = emus_.readString(section(s), "emu_Build");
    e.dateTime = emus_.readInteger(section(s), "emu_DateTime", -1);
    return e;
}

void Frontend::setEmulator(SystemId s, const EmulatorInfo& e) {
    const QString sec = section(s);
    if (!e.isSet()) {
        for (const char* k : {"emu_FileName", "emu_VersionInfo", "emu_DateTime", "emu_Build", "emu_ListXML"})
            emus_.deleteKey(sec, k);
        return;
    }
    emus_.writeString(sec, "emu_FileName", QDir::toNativeSeparators(e.file));
    emus_.writeString(sec, "emu_VersionInfo", e.version);
    emus_.writeInteger(sec, "emu_DateTime", e.dateTime);
    emus_.writeString(sec, "emu_Build", e.build);
}

// GetArcadeEmulatorDescription(…, GetCustomMAMETitle = True). The original looks for "ume" in
// the whole path (so C:\Documents\mame.exe was "UME"); this looks at the file name only.
QString Frontend::description(SystemId s, const QString& exe) {
    if (s == SystemId::HBMAME) return "HBMAME: HomeBrew MAME";
    if (QFileInfo(exe).fileName().contains("ume", Qt::CaseInsensitive)) return "UME: Universal Machine Emulator";
    return "MAME";
}

// Delphi FileAge(): a packed DOS date/time of the file's local modification time.
qint32 Frontend::fileDosDate(const QString& file) {
    QFileInfo fi(file);
    if (!fi.exists()) return -1;
    QDateTime t = fi.lastModified();
    QDate d = t.date();
    QTime tm = t.time();
    if (d.year() < 1980) return -1;
    quint32 dos = quint32(d.year() - 1980) << 25 | quint32(d.month()) << 21 | quint32(d.day()) << 16 |
                  quint32(tm.hour()) << 11 | quint32(tm.minute()) << 5 | quint32(tm.second() / 2);
    return qint32(dos);
}

// GetArcadeEmulatorVersion: "MAME v0.289 (Aug 03 2026)"; the date is the file's (GetDateStr with
// EmuVersionInfo, English month names as EmuLoader writes them).
EmulatorInfo Frontend::describe(SystemId s, const QString& exe, const QString& versionAnswer) {
    EmulatorInfo e;
    e.file = exe;
    e.dateTime = fileDosDate(exe);
    QString answer = versionAnswer.trimmed().section('\n', 0, 0).trimmed();
    e.build = answer.section(' ', 0, 0);
    if (!e.build.isEmpty() && !e.build.at(0).isDigit()) e.build.clear(); // not a version
    QString date;
    QFileInfo fi(exe);
    if (fi.exists()) date = QLocale::c().toString(fi.lastModified().date(), "(MMM dd yyyy)");
    e.version = description(s, exe) + (e.build.isEmpty() ? QString() : " v" + e.build) +
                (date.isEmpty() ? QString() : " " + date);
    return e;
}

int Frontend::mameBuild(SystemId s) const {
    QString b = emus_.readString(section(s), "emu_Build");
    if (b.size() <= 2) return -1;
    bool ok = false;
    int v = b.mid(2).section('.', 0, 0).toInt(&ok); // "0.270" -> 270
    return ok ? v : -1;
}

QString Frontend::mameFolder(SystemId s) const {
    QString exe = emulatorFile(s);
    return exe.isEmpty() ? QString() : QFileInfo(exe).absolutePath();
}

QString Frontend::mameIniPath(SystemId s) const {
    // Port of TFormMain.GetArcadeEmuIniFileName: mame.ini (hbmame.ini) next to EmuLoader
    // ("linux mode"), then next to the emulator or in its ini\ folder, then ume.ini, then
    // <emulator name>.ini; mame.ini in the emulator folder if none exists yet.
    QString exe = emulatorFile(s);
    if (exe.isEmpty()) return {};
    QString base = s == SystemId::HBMAME ? "hbmame.ini" : "mame.ini";
    if (QFileInfo::exists(QDir(dataDir_).filePath(base))) return QDir(dataDir_).filePath(base);
    QDir emu(QFileInfo(exe).absolutePath());
    QString own = QFileInfo(exe).completeBaseName() + ".ini";
    for (const QString& c : {base, "ini/" + base, QString("ume.ini"), QString("ini/ume.ini"), own, "ini/" + own})
        if (QFileInfo::exists(emu.filePath(c))) return emu.filePath(c);
    return emu.filePath("mame.ini");
}

QString Frontend::iniDir(SystemId s) const {
    MameIniFile f;
    f.load(mameIniPath(s));
    return MameIniChain::iniDir(f, mameFolder(s));
}

QStringList Frontend::romFolders(SystemId s) const {
    MameIniFile f;
    f.load(mameIniPath(s));
    QStringList out;
    for (const QString& p : splitPathList(f.get("rompath", "roms"))) out << resolvePath(mameFolder(s), p);
    return out;
}

QStringList Frontend::sampleFolders(SystemId s) const {
    MameIniFile f;
    f.load(mameIniPath(s));
    QStringList out;
    for (const QString& p : splitPathList(f.get("samplepath", "samples"))) out << resolvePath(mameFolder(s), p);
    return out;
}

QString Frontend::mainIcon(const QString& file) const {
    const QString folder = resourcesFolder() + "main_icons/";
    if (ini_.readBool("Preferences", "UseAlternateFrontendIcons", false) && QFileInfo::exists(folder + "alternate/" + file))
        return folder + "alternate/" + file;
    return folder + file;
}

QString Frontend::uiIniPath() const {
    const QString here = QDir(dataDir_).filePath("ui.ini");
    if (QFileInfo::exists(here)) return here;
    const SystemId s = emulator(SystemId::MAME).isSet() ? SystemId::MAME : SystemId::HBMAME;
    const QString there = QDir(mameFolder(s)).filePath("ui.ini");
    return QFileInfo::exists(there) ? there : QString();
}

QString Frontend::iconsSetting() const {
    MameIniFile ui;
    if (const QString u = uiIniPath(); !u.isEmpty() && ui.load(u) && ui.has("icons_directory")) {
        const QString v = ui.get("icons_directory").trimmed();
        return v.isEmpty() ? QStringLiteral("icons") : v;
    }
    WinIni ini;
    ini.load(userFile("arcade/sysimagefolders_arcade.ini"));
    return ini.readString("MAME", "icons_directory", "icons");
}

void Frontend::setIconsSetting(const QString& folders) {
    MameIniFile ui;
    if (const QString u = uiIniPath(); !u.isEmpty() && ui.load(u)) {
        ui.set("icons_directory", folders.isEmpty() ? QStringLiteral("icons") : folders);
        ui.save(u);
        return;
    }
    const QString file = userFile("arcade/sysimagefolders_arcade.ini");
    WinIni ini;
    ini.load(file);
    ini.writeString("MAME", "icons_directory", folders);
    QDir().mkpath(QFileInfo(file).absolutePath());
    ini.save(file);
}

QStringList Frontend::iconFolders() const {
    const SystemId s = emulator(SystemId::MAME).isSet() ? SystemId::MAME : SystemId::HBMAME;
    QStringList out;
    for (const QString& p : splitPathList(iconsSetting())) out << resolvePath(mameFolder(s), p);
    return out;
}

QString Frontend::manualsSetting() const {
    WinIni ini;
    ini.load(userFile("arcade/sysimagefolders_arcade.ini"));
    return ini.readString("MAME", "manuals_directory", "manuals");
}

void Frontend::setManualsSetting(const QString& folder) {
    const QString file = userFile("arcade/sysimagefolders_arcade.ini");
    WinIni ini;
    ini.load(file);
    ini.writeString("MAME", "manuals_directory", folder);
    ini.save(file);
}

QString Frontend::manualsFolder() const {
    const QString f = manualsSetting().trimmed();
    if (f.isEmpty()) return {};
    const QString base = emulator(SystemId::MAME).isSet() ? mameFolder(SystemId::MAME) : mameFolder(SystemId::HBMAME);
    return resolvePath(base, f);
}

QString Frontend::gameManual(const QString& name, const QString& cloneOf) const {
    const QString folder = manualsFolder();
    if (folder.isEmpty() || name.isEmpty()) return {};
    for (const QString& n : {name, cloneOf}) {
        if (n.isEmpty()) continue;
        for (const QString& f : {QDir(folder).filePath(n + ".pdf"), QDir(folder).filePath("manuals/" + n + ".pdf")})
            if (QFileInfo(f).isFile()) return f;
    }
    return {};
}

QString Frontend::gamesFolder() const { return userFile("arcade/games/"); }
QString Frontend::resourcesFolder() const { return QDir(dataDir_).filePath("resources/"); }

ChainMachine Frontend::chainMachine(const MameGame& g) const {
    ChainMachine m;
    m.name = g.name;
    m.cloneOf = g.cloneOf;
    m.bios = g.bios;
    m.driver = g.driver;
    m.orientation = g.orientation;
    m.screenType = g.screenType;
    m.systemType = g.systemType;
    return m;
}

} // namespace eld
