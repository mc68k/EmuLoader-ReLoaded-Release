#include "ImageFinder.h"
#include "ZipArchive.h"

#include <QCoreApplication>
#include "MameIni.h"
#include "TextFile.h"
#include "WinIni.h"

#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <atomic>


namespace eld {

static const ImageCategory kCategories[ImageFinder::Count] = {
    {"image_00_titlesnap", "TitleSnapshot", "title.png", "titles", "titles_directory", QT_TRANSLATE_NOOP("ImageCategory", "Title Snapshot")},
    {"image_01_gamesnap", "GameSnapshot", "ingame.png", "snap", "snapshot_directory", QT_TRANSLATE_NOOP("ImageCategory", "Game Snapshot")},
    {"image_02_marquee", "Marquee", "marquee.png", "marquees", "marquees_directory", QT_TRANSLATE_NOOP("ImageCategory", "Marquee")},
    {"image_03_flyer", "Flyer", "flyer.png", "flyers", "flyers_directory", QT_TRANSLATE_NOOP("ImageCategory", "Flyer")},
    {"image_04_cabinet", "Cabinet", "cabinet.png", "cabinets", "cabinets_directory", QT_TRANSLATE_NOOP("ImageCategory", "Cabinet")},
    {"image_05_cpanel", "ControlPanel", "controlpanel.png", "cpanel", "cpanels_directory", QT_TRANSLATE_NOOP("ImageCategory", "Control Panel")},
    {"image_06_cover", "SoftwareCover", "softwarecover.png", "covers", "covers_directory", QT_TRANSLATE_NOOP("ImageCategory", "Software Cover")},
    {"image_07_pcb", "PCB", "pcb.png", "pcb", "pcbs_directory", QT_TRANSLATE_NOOP("ImageCategory", "PCB")},
    {"image_08_gameartwork", "GameArtwork", "ingameartwork.png", "artpreview", "artwork_preview_directory", QT_TRANSLATE_NOOP("ImageCategory", "Game Artwork")},
    {"image_09_end", "End", "end.png", "ends", "ends_directory", QT_TRANSLATE_NOOP("ImageCategory", "End")},
    {"image_10_boss", "Boss", "boss.png", "bosses", "bosses_directory", QT_TRANSLATE_NOOP("ImageCategory", "Boss")},
    {"image_11_logo", "Logo", "logo.png", "logo", "logos_directory", QT_TRANSLATE_NOOP("ImageCategory", "Logo")},
    {"image_12_score", "Score", "score.png", "scores", "scores_directory", QT_TRANSLATE_NOOP("ImageCategory", "Score")},
    {"image_13_versus", "Versus", "versus.png", "versus", "versus_directory", QT_TRANSLATE_NOOP("ImageCategory", "Versus")},
    {"image_14_gameover", "GameOver", "gameover.png", "gameover", "gameover_directory", QT_TRANSLATE_NOOP("ImageCategory", "Game Over")},
    {"image_15_howto", "HowToPlay", "howtoplay.png", "howto", "howto_directory", QT_TRANSLATE_NOOP("ImageCategory", "How to Play")},
    {"image_16_select", "Select", "select.png", "select", "select_directory", QT_TRANSLATE_NOOP("ImageCategory", "Select")},
};

static const ImageCategory kIcons = {"image_icons", "Icons", "", "icons", "icons_directory",
                                     QT_TRANSLATE_NOOP("ImageCategory", "Icons (MAMu_)")};

const ImageCategory& ImageFinder::category(int i) { return i == Icons ? kIcons : kCategories[qBound(0, i, Count - 1)]; }

QString ImageFinder::title(int i) { return QCoreApplication::translate("ImageCategory", category(i).title); }

QStringList ImageFinder::readSettings(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni,
                                      const QString& section) {
    QStringList value;
    for (int i = 0; i < Count; ++i) value << QString();
    // ui.ini: next to EmuLoader ("linux mode"), else next to MAME (ReadMAMESettingUI)
    MameIniFile ui;
    bool haveUi = ui.load(QDir(dataDir).filePath("ui.ini")) || ui.load(QDir(mameFolder).filePath("ui.ini"));
    WinIni cat;
    bool haveCat = !haveUi && cat.load(QDir(userDir).filePath("arcade/sysimagefolders_arcade.ini"));
    for (int i = 0; i < Count; ++i) {
        if (i == 1) continue; // snapshots: mame.ini
        if (haveUi) value[i] = ui.get(kCategories[i].option);
        else if (haveCat) value[i] = cat.readString(section, kCategories[i].option);
    }
    MameIniFile mi;
    if (mi.load(mameIni)) value[1] = mi.get("snapshot_directory");
    return value;
}

bool ImageFinder::writeSettings(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni,
                                const QString& section, const QStringList& values) {
    bool ok = true;
    auto value = [&](int i) { return values.value(i).trimmed(); };
    QString uiPath = QDir(dataDir).filePath("ui.ini");
    if (!QFileInfo::exists(uiPath)) uiPath = QDir(mameFolder).filePath("ui.ini");
    MameIniFile ui;
    if (!mameFolder.isEmpty() && ui.load(uiPath)) {
        // WriteMAMESettingUI: an empty folder is written as MAME's default one
        bool changed = false;
        for (int i = 0; i < Count; ++i) {
            if (i == 1) continue;
            // a line MAME's ui.ini doesn't have yet is added only for a folder that was set
            if (!ui.has(kCategories[i].option) && value(i).isEmpty()) continue;
            QString v = value(i).isEmpty() ? QString(kCategories[i].folder) : value(i);
            if (i == 8 && value(i).isEmpty()) v = "artwork preview;artpreview";
            changed |= ui.set(kCategories[i].option, v);
        }
        if (changed) ok &= ui.save(uiPath);
    } else {
        QString file = QDir(userDir).filePath("arcade/sysimagefolders_arcade.ini");
        WinIni cat;
        cat.load(file);
        for (int i = 0; i < Count; ++i) {
            if (i == 1) continue;
            if (value(i).isEmpty()) cat.deleteKey(section, kCategories[i].option);
            else cat.writeString(section, kCategories[i].option, value(i));
        }
        QDir().mkpath(QFileInfo(file).absolutePath());
        ok &= cat.save(file);
    }
    // UpdateMAMEsnapDir: snapshot_directory in mame.ini ("snap" when empty)
    MameIniFile mi;
    if (mi.load(mameIni) && mi.has("snapshot_directory")) {
        QString v = value(1).isEmpty() ? QStringLiteral("snap") : value(1);
        while (v.size() > 1 && (v.endsWith('\\') || v.endsWith('/'))) v.chop(1);
        if (mi.set("snapshot_directory", v)) ok &= mi.save(mameIni);
    }
    return ok;
}

void ImageFinder::setup(const QString& dataDir, const QString& userDir, const QString& mameFolder, const QString& mameIni, const QString& section) {
    folders_.clear();
    zipCache_.clear();
    QStringList value = readSettings(dataDir, userDir, mameFolder, mameIni, section);
    for (int i = 0; i < Count; ++i) {
        QString v = value[i];
        if (i == 8 && v.compare("artwork preview;artpreview", Qt::CaseInsensitive) == 0) v = "artpreview";
        if (v.isEmpty()) v = kCategories[i].folder;
        QStringList dirs;
        for (const QString& p : splitPathList(v)) dirs << resolvePath(mameFolder, p);
        folders_.insert(i, dirs);
    }
}

const QSet<QString>& ImageFinder::zipEntries(const QString& zip) const {
    auto it = zipCache_.find(zip);
    if (it != zipCache_.end()) return *it;
    QSet<QString> names;
    if (auto za = ZipArchive::shared(zip))
        for (const ZipArchive::Entry& e : za->entries()) names.insert(e.name.toLower());
    return *zipCache_.insert(zip, names);
}

void ImageFinder::forgetZips() const {
    zipCache_.clear();
    ZipArchive::release();
}

ImageFinder::Found ImageFinder::findIn(int c, const QString& name, int index) const {
    const ImageCategory& cat = kCategories[c];
    QStringList exts = {"png"};
    if (!pngOnly(c)) exts << "jpg";
    // GetImageName: index 1 = <name>; EmuLoader format <name>0000 (index 2 ...);
    // MAME format <name>\0000 for snapshots (index 1 ...).
    QStringList names;
    if (index == 1) names << name;
    else names << name + QString("%1").arg(index - 2, 4, 10, QChar('0'));
    if (c == 1) names << name + "/" + QString("%1").arg(index - 1, 4, 10, QChar('0'));
    for (const QString& ext : exts)
        for (const QString& n : names)
            for (const QString& dir : folders_.value(c)) {
                for (const QString& sub : {QString(), QString(cat.folder) + "/"}) {
                    QString f = QDir(dir).filePath(sub + n + "." + ext);
                    if (QFileInfo::exists(f)) return {true, f, {}, ext.toUpper()};
                }
            }
    // <category>.zip in the folder, or next to it (MAME's own layout: snap.zip beside snap\);
    // a software list game's also in <list>.zip, <list>\<list>.zip and <category>_sl.zip
    const int slash = name.indexOf('/');
    const QString list = slash > 0 ? name.left(slash) : QString();
    for (const QString& dir : folders_.value(c)) {
        QStringList zips{QDir(dir).filePath(QString(cat.folder) + ".zip"), dir + ".zip"};
        if (!list.isEmpty())
            zips << QDir(dir).filePath(list + ".zip") << QDir(dir).filePath(list + "/" + list + ".zip")
                 << QDir(dir).filePath(QString(cat.folder) + "_sl.zip");
        for (const QString& zip : withSevenZip(zips)) {
            if (!QFileInfo::exists(zip)) continue;
            const QString prefix = zipPrefix(zip);
            const QSet<QString>& entries = zipEntries(zip);
            for (const QString& ext : exts)
                for (const QString& n : names) {
                    QString e = (n + "." + ext).toLower();
                    if (!prefix.isEmpty()) {
                        if (!e.startsWith(prefix)) continue;
                        e = e.mid(prefix.size());
                    }
                    if (entries.contains(e)) return {true, zip, e, ext.toUpper()};
                }
        }
    }
    return {};
}

void ImageFinder::setSoftwareLists(const QStringList& lists) {
    softwareLists_.clear();
    for (const QString& l : lists) softwareLists_.insert(l.toLower());
}

// Each zip, then the same name as a .7z (snap.zip, snap.7z): MAME reads both, and so do we
QStringList ImageFinder::withSevenZip(const QStringList& zips) {
    QStringList out;
    for (const QString& z : zips) {
        out << z;
        if (z.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) out << z.chopped(4) + ".7z";
    }
    return out;
}

QString ImageFinder::zipPrefix(const QString& zip) const {
    const QString base = QFileInfo(zip).completeBaseName().toLower();
    return softwareLists_.contains(base) ? base + "/" : QString();
}

QStringList ImageFinder::gameFiles(int c, const QString& name) const {
    QStringList out;
    if (c < 0 || c >= Count || name.isEmpty()) return out;
    const ImageCategory& cat = kCategories[c];
    QStringList exts = {"png"};
    if (!pngOnly(c)) exts << "jpg";
    auto add = [&](const QString& f) {
        if (QFileInfo(f).isFile() && !out.contains(f)) out << f;
    };
    for (const QString& dir : folders_.value(c))
        for (const QString& sub : {QString(), QString(cat.folder) + "/"}) {
            QDir d(QDir(dir).filePath(sub));
            if (!d.exists()) continue;
            for (const QString& ext : exts) {
                add(d.filePath(name + "." + ext));
                for (int i = 0; i < MaxImages - 1; ++i)
                    add(d.filePath(name + QString("%1").arg(i, 4, 10, QChar('0')) + "." + ext));
            }
            if (c == 1) // MAME's <name>\0000.png
                for (int i = 0; i < MaxImages; ++i) add(d.filePath(name + "/" + QString("%1").arg(i, 4, 10, QChar('0')) + ".png"));
        }
    return out;
}

QStringList ImageFinder::unusedFiles(int c, const QStringList& names) const {
    QStringList out;
    if (c < 0 || c >= Count) return out;
    const ImageCategory& cat = kCategories[c];
    QStringList exts = {"png"};
    if (!pngOnly(c)) exts << "jpg";
    QSet<QString> games;
    for (const QString& n : names) games.insert(n.toLower());
    const QString sub = QString(cat.folder).toLower() + "/";
    auto isIndex = [](const QString& s) {
        if (s.size() != 4) return false;
        for (QChar ch : s) if (!ch.isDigit()) return false;
        return true;
    };
    // A picture's name relative to a category folder: <name>.png, <name>0000.png ... (EmuLoader),
    // <name>\0000.png (MAME, snapshots), each also in a <category folder>\ subfolder.
    auto used = [&](QString rel) {
        rel = rel.toLower();
        if (rel.startsWith(sub)) rel = rel.mid(sub.size());
        QString ext = QFileInfo(rel).suffix();
        QString base = rel.left(rel.size() - ext.size() - 1);
        int slash = base.indexOf('/');
        if (slash >= 0) // MAME's <name>\0000.png
            return c == 1 && ext == "png" && base.indexOf('/', slash + 1) < 0 && isIndex(base.mid(slash + 1)) &&
                   games.contains(base.left(slash));
        if (games.contains(base)) return true;
        return base.size() > 4 && isIndex(base.right(4)) && base.right(4).toInt() < MaxImages - 1 &&
               games.contains(base.left(base.size() - 4));
    };
    QSet<QString> seen;
    for (const QString& dir : folders_.value(c)) {
        QDir root(dir);
        if (!root.exists()) continue;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString f = it.next();
            if (!exts.contains(QFileInfo(f).suffix().toLower())) continue;
            QString key = QFileInfo(f).absoluteFilePath();
            if (seen.contains(key)) continue; // a folder listed twice, or inside another
            seen.insert(key);
            if (!used(root.relativeFilePath(f))) out << f;
        }
    }
    return out;
}

QString ImageFinder::renamedFor(const QString& file, QString from, QString to) {
    // a software list game's (nes/smbj -> nes/smb): the list's folder stays
    if (from.contains('/')) from = from.section('/', -1);
    if (to.contains('/')) to = to.section('/', -1);
    QFileInfo fi(file);
    QString base = fi.completeBaseName();
    QDir dir = fi.dir();
    // MAME's <name>\0000.png
    if (dir.dirName().compare(from, Qt::CaseInsensitive) == 0 && base.size() == 4 &&
        base.toInt() >= 0 && base.at(0).isDigit()) {
        QDir up = dir;
        up.cdUp();
        return up.filePath(to + "/" + fi.fileName());
    }
    if (!base.startsWith(from, Qt::CaseInsensitive)) return {};
    QString rest = base.mid(from.size());
    if (!rest.isEmpty() && !(rest.size() == 4 && rest.toInt() >= 0 && rest.at(0).isDigit())) return {};
    return dir.filePath(to + rest + "." + fi.suffix());
}

QStringList ImageFinder::zipFiles(int c) const {
    QStringList out;
    if (c < 0 || c >= CountWithIcons) return out;
    for (const QString& dir : folders_.value(c)) {
        QStringList zips{QDir(dir).filePath(QString(category(c).folder) + ".zip"), QDir::cleanPath(dir) + ".zip"};
        if (c != Icons) {
            zips << QDir(dir).filePath(QString(category(c).folder) + "_sl.zip");
            for (const QString& l : softwareLists_) zips << QDir(dir).filePath(l + ".zip") << QDir(dir).filePath(l + "/" + l + ".zip");
        }
        for (const QString& zip : withSevenZip(zips)) {
            QString abs = QFileInfo(zip).absoluteFilePath();
            if (QFileInfo(abs).isFile() && !out.contains(abs)) out << abs;
        }
    }
    return out;
}

ImageFinder::Found ImageFinder::find(int c, const QString& name, const QString& parent, int index) const {
    Found f = findIn(c, name, index);
    if (!f.ok && !parent.isEmpty()) f = findIn(c, parent, index);
    return f;
}

static std::atomic<int> g_readsStarted{0};
int ImageFinder::readsStarted() { return g_readsStarted; }

QByteArray ImageFinder::read(const Found& f, const std::function<bool()>& keepGoing) {
    if (!f.ok || (keepGoing && !keepGoing())) return {};
    ++g_readsStarted;
    if (!f.zipped()) {
        QFile file(f.file);
        return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
    }
    auto za = ZipArchive::shared(f.file);
    return za ? za->read(f.entry, keepGoing) : QByteArray();
}

} // namespace eld
