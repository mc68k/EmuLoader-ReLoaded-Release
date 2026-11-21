#include "ElDatabase.h"
#include "TextFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace eld {

int MameGame::gameId() const {
    int base = int(kind);                 // 0 parent, 1 clone, 2 bios, 3 device
    return have() ? base : base + 4;      // 04..07 = missing
}

const MameGame* ElDatabase::find(const QString& name) const {
    int i = indexOf(name);
    return i < 0 ? nullptr : &games_[i];
}

void ElDatabase::reindex() {
    index_.clear();
    index_.reserve(games_.size());
    for (int i = 0; i < games_.size(); ++i) index_.insert(games_[i].key(), i);
}

void ElDatabase::setSoftware(QVector<MameGame> games) {
    games_.erase(std::remove_if(games_.begin(), games_.end(), [](const MameGame& g) { return g.isSoftware(); }), games_.end());
    softwareCount_ = int(games.size());
    games_.reserve(games_.size() + games.size());
    for (MameGame& g : games) games_.push_back(std::move(g));
    reindex();
}

// ------------------------------------------------------------------- .el read

static qint8 toI8(const QString& s, qint8 def = -1) {
    bool ok = false;
    int v = s.toInt(&ok);
    return ok ? qint8(v) : def;
}

bool ElDatabase::load(const QString& elFile, const QString& statusFile, QString* error) {
    QString text;
    utf8_ = false;
    if (!readTextFile(elFile, text, &utf8_)) {
        if (error) *error = "Cannot read " + elFile;
        return false;
    }
    games_.clear();
    softwareCount_ = 0;
    description.clear();
    version.clear();
    games_.reserve(text.size() / 180);
    static const QRegularExpression tagRe("<(\\w+)>([^<]*?)/>");
    QString section;
    for (QStringView line : QStringView(text).split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty() || line.startsWith(';')) continue;
        if (line.startsWith('[')) { section = line.mid(1, line.size() - 2).toString(); continue; }
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        if (section == "info_file") { // list header, not a game
            if (line.startsWith(u"description=")) description = line.mid(eq + 1).toString();
            else if (line.startsWith(u"version=")) version = line.mid(eq + 1).toString();
            continue;
        }
        MameGame g;
        g.name = line.left(eq).toString();
        g.raw = line.toString();
        g.section = section;
        // Device and BIOS sets are grouped in their own sections.
        if (section == "devices") g.kind = SetKind::Device;
        else if (section == "bios") g.kind = SetKind::Bios;
        QStringView rest = line.mid(eq + 1);
        int lt = rest.indexOf('<');
        g.mediaType = toI8(rest.left(lt < 0 ? rest.size() : lt).left(1).toString(), 0);
        auto it = tagRe.globalMatch(rest.mid(lt < 0 ? rest.size() : lt));
        while (it.hasNext()) {
            auto m = it.next();
            QString tag = m.captured(1), v = m.captured(2);
            if (tag == "title") g.title = v;
            else if (tag == "year") g.year = v;
            else if (tag == "manuf" || tag == "publisher") g.manufacturer = v;
            else if (tag == "ctrl") g.controls = v;
            else if (tag == "btn") g.buttons = v.toShort();
            else if (tag == "scrtype") g.screenType = toI8(v);
            else if (tag == "scrorient") g.orientation = toI8(v);
            else if (tag == "scrres") g.resolution = v;
            else if (tag == "scrrefresh") g.refresh = v;
            else if (tag == "audiotype") g.audioType = toI8(v, 0);
            else if (tag == "drvstat") g.driverStatus = toI8(v);
            else if (tag == "bios") g.bios = v;
            else if (tag == "cloneof") g.cloneOf = v;
            else if (tag == "drvname") g.driver = v;
            else if (tag == "nplayer") g.players = v.toShort();
            else if (tag == "size") g.size = v.toLongLong();
            else if (tag == "emustat") g.emuStatus = toI8(v);
            else if (tag == "clrstat") g.colorStatus = toI8(v);
            else if (tag == "sndstat") g.soundStatus = toI8(v);
            else if (tag == "grpstat") g.graphicStatus = toI8(v);
            else if (tag == "protectionstat") g.protectionStatus = toI8(v);
            else if (tag == "cocktailstat") g.cocktailStatus = toI8(v);
            else if (tag == "lanstat") g.lanStatus = toI8(v);
            else if (tag == "sta") g.saveState = toI8(v);
            else if (tag == "mechanic") g.mechanical = v == "1";
            else if (tag == "systype") g.systemType = toI8(v, 0);
            else if (tag == "romflags") g.romFlags = quint8(v.toUInt());
            else if (tag == "romof") g.romOf = v;
            else if (tag == "maincpu") g.mainCpu = v;
        }
        // Flag tags have no value and no "/>": <mech>, <reqartwork>
        if (rest.contains(u"<mech>")) g.mechanical = true;
        if (rest.contains(u"<reqartwork>")) g.requiresArtwork = true;
        if (!g.cloneOf.isEmpty() && g.kind == SetKind::Parent) g.kind = SetKind::Clone;
        games_.push_back(std::move(g));
    }
    reindex();

    // .elstatus: name=GGSMm  (GameID, set status, scan mode, merged)
    QString st;
    if (!statusFile.isEmpty() && readTextFile(statusFile, st)) {
        for (QStringView line : QStringView(st).split('\n')) {
            if (line.endsWith('\r')) line.chop(1);
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            int i = indexOf(line.left(eq).toString());
            QStringView v = line.mid(eq + 1);
            if (i < 0 || v.size() < 3) continue;
            MameGame& g = games_[i];
            int id = v.left(2).toInt();
            g.kind = SetKind(id % 4);
            g.setStatus = qint8(v[2].digitValue());
            if (v.size() > 3) g.scanMode = qint8(v[3].digitValue());
            if (v.size() > 4) g.merged = v[4] == '1';
        }
    }
    return true;
}

QString MameGame::driverText(bool withFolder) const {
    if (withFolder) return driver;
    int slash = std::max(driver.lastIndexOf('/'), driver.lastIndexOf('\\'));
    return slash < 0 ? driver : driver.mid(slash + 1);
}

// ------------------------------------------------------------------ .el write

static QString esc(QString s) { return s.replace('\n', ' ').replace('\r', ' '); }

static QString buildLine(const MameGame& g) {
    QString s = g.name + "=" + QString::number(g.mediaType);
    auto tag = [&](const char* t, const QString& v) { if (!v.isEmpty()) s += QString("<%1>%2/>").arg(t, esc(v)); };
    auto num = [&](const char* t, int v) { if (v >= 0) s += QString("<%1>%2/>").arg(t).arg(v); };
    tag("title", g.title); tag("year", g.year); tag("manuf", g.manufacturer); tag("ctrl", g.controls);
    num("btn", g.buttons); num("scrtype", g.screenType); num("scrorient", g.orientation);
    tag("scrres", g.resolution); tag("scrrefresh", g.refresh); num("audiotype", g.audioType);
    num("drvstat", g.driverStatus);
    // A BIOS set's <bios> is its own name, as EmuLoader writes it (it warns "BIOS set name is blank" otherwise).
    tag("bios", g.kind == SetKind::Bios && g.bios.isEmpty() ? g.name : g.bios); tag("cloneof", g.cloneOf); tag("drvname", g.driver);
    num("nplayer", g.players);
    if (g.mechanical) s += "<mech>";
    if (g.size > 0) s += QString("<size>%1/>").arg(g.size);
    num("emustat", g.emuStatus); num("clrstat", g.colorStatus); num("sndstat", g.soundStatus);
    num("grpstat", g.graphicStatus); num("protectionstat", g.protectionStatus); num("cocktailstat", g.cocktailStatus);
    num("sta", g.saveState); num("lanstat", g.lanStatus); // the original's order
    if (g.requiresArtwork) s += "<reqartwork>";
    if (g.romFlags) s += QString("<romflags>%1/>").arg(g.romFlags); // this version's
    if (!g.romOf.isEmpty()) s += QString("<romof>%1/>").arg(g.romOf); // this version's
    if (!g.mainCpu.isEmpty()) s += QString("<maincpu>%1/>").arg(g.mainCpu); // this version's
    return s;
}

static QString statusLine(const MameGame& g) {
    return QString("%1=%2%3%4%5\r\n").arg(g.name).arg(g.gameId(), 2, 10, QChar('0')).arg(g.setStatus).arg(g.scanMode).arg(g.merged ? 1 : 0);
}

bool ElDatabase::save(const QString& elFile, const QString& statusFile) const {
    QString out;
    out.reserve(games_.size() * 200);
    // EmuLoader expects exactly these 4 lines first (it drops lines 0-3 when it finds them).
    out += "[info_file]\r\ndescription=" + description + "\r\nversion=" + version + "\r\n\r\n";
    // Games are kept in file order, grouped by section ([devices], [bios], then one section
    // per parent with its clones). Lines read from a file are written back unchanged.
    QString cur;
    bool first = true;
    for (const MameGame& g : games_) {
        if (g.isSoftware()) continue;
        if (first || g.section != cur) {
            if (!first) out += "\r\n";
            out += "[" + g.section + "]\r\n";
            cur = g.section;
            first = false;
        }
        out += (g.raw.isEmpty() ? buildLine(g) : g.raw) + "\r\n";
    }
    // Same encoding as the file had (ANSI for new lists, like TStringList.SaveToFile).
    return writeTextFile(elFile, out, utf8_) && (statusFile.isEmpty() || saveStatus(statusFile));
}

bool ElDatabase::saveStatus(const QString& statusFile) const {
    QString status;
    status.reserve(games_.size() * 16);
    // Same order as EmuLoader writes it: devices and BIOS sets as in the list, then every
    // other set sorted by name.
    QVector<const MameGame*> rest;
    for (const MameGame& g : games_)
        if (g.isSoftware()) continue;
        else if (g.kind == SetKind::Device || g.kind == SetKind::Bios) status += statusLine(g);
        else rest.push_back(&g);
    std::sort(rest.begin(), rest.end(), [](const MameGame* a, const MameGame* b) { return a->name.compare(b->name) < 0; });
    for (const MameGame* g : rest) status += statusLine(*g);
    return writeTextFile(statusFile, status, false);
}

bool ElDatabase::saveBiosSets(const QString& file) const {
    // Written the way TMemIniFile writes it in TFormMain (one section after the other).
    QString out = "[Descriptions]\r\n";
    for (const BiosSets& b : biosSets) out += b.bios + "=" + b.title + "\r\n";
    out += "\r\n";
    for (const BiosSets& b : biosSets) {
        out += "[" + b.bios + "]\r\ndefault_bios=" + b.defaultSet + "\r\n";
        for (const auto& s : b.sets) out += s.first + "=" + s.second + "\r\n";
        out += "\r\n";
    }
    return writeTextFile(file, out, false);
}

bool ElDatabase::saveRamSizes(const QString& file) const {
    // UpdateRAMSizeFile: a section per machine, a blank line between them
    QString out;
    for (const RamSizes& r : ramSizes) {
        if (!out.isEmpty()) out += "\r\n";
        out += "[" + r.machine + "]\r\ndefault=" + r.defaultName + "\r\n";
        for (const auto& o : r.options) out += o.first + "=" + o.second + "\r\n";
    }
    QFile::remove(file);
    if (out.isEmpty()) return true; // no machine has RAM choices: no file (as the original)
    return writeTextFile(file, out, false);
}

ElDatabase::RamSizes ElDatabase::loadRamSizes(const QString& file, const QString& machine) {
    RamSizes r;
    r.machine = machine;
    QString text;
    if (!readTextFile(file, text)) return r;
    bool in = false;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.startsWith(u'[')) {
            if (in) break;
            in = line.mid(1, line.indexOf(u']') - 1).compare(machine, Qt::CaseInsensitive) == 0;
            continue;
        }
        const qsizetype eq = line.indexOf(u'=');
        if (!in || eq <= 0) continue;
        const QString key = line.left(eq).toString(), value = line.mid(eq + 1).toString();
        if (key.compare(QLatin1String("default"), Qt::CaseInsensitive) == 0) r.defaultName = value;
        else r.options.append({key, value});
    }
    return r;
}

bool ElDatabase::saveSamples(const QString& file) const {
    QStringList names = samples.keys();
    names.sort();
    QString out;
    for (const QString& n : names) out += n + "=" + samples.value(n) + "\r\n";
    return writeTextFile(file, out, false);
}

QHash<QString, QString> ElDatabase::loadSamples(const QString& file) {
    QHash<QString, QString> out;
    QString text;
    if (!readTextFile(file, text)) return out;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        const qsizetype eq = line.indexOf(u'=');
        if (eq > 0) out.insert(line.left(eq).toString(), line.mid(eq + 1).toString());
    }
    return out;
}

bool ElDatabase::loadSoftwareLists(const QString& file) {
    QString text;
    if (!readTextFile(file, text)) return false;
    MameGame* cur = nullptr;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';')) continue;
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            int i = indexOf(line.mid(1, line.size() - 2).toString());
            cur = i < 0 ? nullptr : &games_[i];
            if (cur) cur->softwareLists.clear();
            continue;
        }
        if (cur) {
            cur->softwareLists << line.toString();
            cur->systemType = 1;
        }
    }
    return true;
}

bool ElDatabase::saveSoftwareLists(const QString& file) const {
    QString out;
    for (const MameGame& g : games_) {
        if (g.softwareLists.isEmpty() || g.isSoftware()) continue;
        out += "[" + g.name + "]\r\n";
        for (const QString& s : g.softwareLists) out += s + "\r\n";
    }
    return writeTextFile(file, out, false);
}

int ElDatabase::loadMessIni(const QString& file) {
    // AntoPISA's mess.ini: a folder list of non-arcade machine names (NonArcadeMESSIni).
    QString text;
    if (!readTextFile(file, text)) return 0;
    int n = 0;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'[') || line.contains(u' ')) continue;
        int i = indexOf(line.toString());
        if (i >= 0 && games_[i].systemType == 0) { games_[i].systemType = 1; ++n; }
    }
    return n;
}

int ElDatabase::loadLanguages(const QString& file) {
    QString text;
    if (!readTextFile(file, text)) return 0;
    int n = 0;
    QString section;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';')) continue;
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            section = line.mid(1, line.size() - 2).toString();
            continue;
        }
        // [FOLDER_SETTINGS] / [ROOT_FOLDER] and "key=value" lines aren't languages
        if (section.isEmpty() || section.startsWith(u"FOLDER_SETTINGS") || section == u"ROOT_FOLDER" || line.contains(u'='))
            continue;
        const int i = indexOf(line.toString());
        if (i < 0 || !games_[i].language.isEmpty()) continue; // the first language listed
        games_[i].language = section;
        ++n;
    }
    return n;
}

int ElDatabase::loadNPlayers(const QString& file) {
    QString text;
    if (!readTextFile(file, text)) return 0;
    int n = 0;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';') || line.startsWith(u'[')) continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0) continue;
        const int i = indexOf(line.left(eq).trimmed().toString());
        if (i < 0) continue;
        games_[i].playersIni = line.mid(eq + 1).trimmed().toString();
        ++n;
    }
    return n;
}

bool ElDatabase::loadCatver(const QString& file) {
    QString text;
    if (!readTextFile(file, text)) return false;
    QString section;
    for (QStringView line : QStringView(text).split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith('[')) { section = line.mid(1, line.size() - 2).toString(); continue; }
        if (section != "Category" && section != "VerAdded") continue;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        int i = indexOf(line.left(eq).toString());
        if (i < 0) continue;
        if (section == "Category") games_[i].category = line.mid(eq + 1).toString();
        else games_[i].versionAdded = line.mid(eq + 1).toString();
    }
    return true;
}

// --------------------------------------------------------------- -listxml

static QString attr(QStringView line, const char* name) {
    QString key = QString(" ") + name + "=\"";
    int p = line.indexOf(key);
    if (p < 0) return {};
    p += key.size();
    int e = line.indexOf('"', p);
    QString v = line.mid(p, e - p).toString();
    if (v.contains('&'))
        v.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
    return v;
}

static QString text(QStringView line, const char* tag) {
    QString open = QString("<") + tag + ">";
    int p = line.indexOf(open);
    if (p < 0) return {};
    p += open.size();
    int e = line.indexOf(u"</", p);
    QString v = line.mid(p, e - p).toString();
    if (v.contains('&'))
        v.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
    return v;
}

static qint8 status(const QString& s) {
    return s == "good" ? 0 : s == "imperfect" ? 1 : s == "preliminary" ? 2 : -1;
}

bool ElDatabase::fromListXml(const std::function<bool(QString&)>& nextLine, const Progress& progress,
                             const std::atomic<bool>* cancel) {
    QVector<MameGame> list;
    list.reserve(50000);
    biosSets.clear();
    memoryCards.clear();
    ramSizes.clear();
    RamSizes ram;
    samples.clear();
    MameGame g;
    QString sampleOf;
    bool in = false, skip = false, haveDisplay = false, hasCoins = false;
    // ParseMemCardDeviceTag: the extensions of the machine's first memory card, the slots' options.
    bool inMemcard = false;
    MemoryCardSlots cards;
    QList<QPair<MameGame, MemoryCardSlots>> withCards; // the section is known once the BIOS is
    QStringList softlists;
    QSet<QString> ctrls;
    QSet<QString> biosSets;
    // The machine's ROMs and devices, for MameGame::romFlags once the whole list is read.
    struct Rom { QString crc, sha1; };
    struct Roms { QVector<Rom> roms; QStringList devices; bool hashed = false; bool ownFiles = false;
                  QStringList unmerged; }; // the dumped files without merge="...": "crc", or "sha1:<sha1>" for a disk
    Roms roms;
    QHash<QString, Roms> romsOf;
    QString line;
    while (nextLine(line)) {
        if (cancel && *cancel) return false;
        QStringView l = QStringView(line).trimmed();
        if (l.startsWith(u"<machine ") || l.startsWith(u"<game ")) {
            g = MameGame{};
            in = true;
            haveDisplay = hasCoins = false;
            softlists.clear();
            ctrls.clear();
            cards = MemoryCardSlots{};
            ram = RamSizes{};
            inMemcard = false;
            roms = Roms{};
            g.name = attr(l, "name");
            g.cloneOf = attr(l, "cloneof");
            sampleOf = attr(l, "sampleof");
            QString romof = attr(l, "romof");
            // As MAME gives it: "pacman/pacman.cpp" since 0.2xx (the original stores it the same way).
            // The games list can show it with or without the folder (Preferences); driver
            // custom settings use the file name only (source\pacman.ini), as MAME does.
            g.driver = attr(l, "sourcefile");
            if (attr(l, "isbios") == "yes") { g.kind = SetKind::Bios; biosSets.insert(g.name); }
            else if (attr(l, "isdevice") == "yes") g.kind = SetKind::Device;
            else if (!g.cloneOf.isEmpty()) g.kind = SetKind::Clone;
            g.bios = romof; // resolved to the real BIOS after the whole list is read
            g.mechanical = attr(l, "ismechanical") == "yes";
            skip = attr(l, "runnable") == "no" && g.kind != SetKind::Device;
            if (l.endsWith(u"/>")) in = false;
            continue;
        }
        if (!in) continue;
        if (l.startsWith(u"</machine") || l.startsWith(u"</game")) {
            in = false;
            if (skip || g.name.isEmpty()) continue;
            // Device sets without ROMs are left out (EmuLoader's default: they can't be run
            // and aren't needed to validate games).
            if (g.kind == SetKind::Device && g.size == 0 && g.mediaType == 0) continue;
            QStringList c(ctrls.begin(), ctrls.end());
            c.sort();
            g.controls = c.join(',');
            // As TFormMain.CreateGamesListXML: a machine with software lists is not arcade.
            g.softwareLists = softlists;
            g.systemType = softlists.isEmpty() ? 0 : 1;
            if (!ram.options.isEmpty()) {
                ram.machine = g.name;
                ramSizes.append(ram);
            }
            if (!cards.options.isEmpty() && g.kind != SetKind::Device && g.kind != SetKind::Bios)
                withCards.append({g, cards});
            romsOf.insert(g.name, roms);
            // the audio samples set the machine uses (ListSamples: sampleof, or its own name)
            if (g.audioType == 2) samples.insert(g.name, sampleOf.isEmpty() ? g.name : sampleOf);
            list.push_back(g);
            if (progress && list.size() % 500 == 0) progress(list.size());
            continue;
        }
        if (inMemcard) {
            if (l.startsWith(u"</device")) inMemcard = false;
            else if (l.startsWith(u"<instance ")) {
                QString n = attr(l, "name");
                if (!n.isEmpty() && cards.options.size() < 4) cards.options << n;
            } else if (l.startsWith(u"<extension ") && cards.options.size() <= 1) {
                QString e = attr(l, "name"); // the first card's extensions only
                if (!e.isEmpty() && !cards.extensions.contains(e)) cards.extensions << e;
            }
            continue;
        }
        if (l.startsWith(u"<device ") && attr(l, "type") == "memcard" && !l.endsWith(u"/>")) {
            inMemcard = true;
            continue;
        }
        if (l.startsWith(u"<ramoption ")) { // ParseRAMOption: <ramoption name="64K" default="yes">65536</ramoption>
            const QString name = attr(l, "name");
            const qsizetype gt = l.indexOf(u'>'), lt = gt < 0 ? -1 : l.indexOf(u'<', gt + 1);
            const QString size = gt >= 0 && lt > gt ? l.mid(gt + 1, lt - gt - 1).toString() : QString();
            if (attr(l, "default") == "yes") ram.defaultName = name;
            if (!name.isEmpty() && !size.isEmpty()) ram.options.append({name, size});
            continue;
        }
        if (l.startsWith(u"<description")) g.title = text(l, "description");
        else if (l.startsWith(u"<year")) g.year = text(l, "year");
        else if (l.startsWith(u"<manufacturer")) g.manufacturer = text(l, "manufacturer");
        else if (l.startsWith(u"<rom ")) {
            g.size += attr(l, "size").toLongLong();
            Rom r{attr(l, "crc"), attr(l, "sha1")};
            // a file in the set's own zip (split sets): not its parent's, and dumped
            if (attr(l, "merge").isEmpty() && attr(l, "status") != "nodump") {
                roms.ownFiles = true;
                roms.unmerged << (r.crc.isEmpty() ? "sha1:" + r.sha1 : r.crc);
            }
            roms.hashed |= !r.crc.isEmpty() || !r.sha1.isEmpty();
            roms.roms.push_back(r);
        } else if (l.startsWith(u"<disk ")) {
            g.mediaType = 1;
            Rom r{{}, attr(l, "sha1")};
            if (attr(l, "merge").isEmpty() && attr(l, "status") != "nodump") {
                roms.ownFiles = true;
                roms.unmerged << "sha1:" + r.sha1;
            }
            roms.hashed |= !r.sha1.isEmpty();
            roms.roms.push_back(r);
        } else if (l.startsWith(u"<device_ref ")) {
            roms.devices << attr(l, "name");
        } else if (!haveDisplay && l.startsWith(u"<display ")) {
            haveDisplay = true;
            QString t = attr(l, "type");
            g.screenType = t == "raster" ? 0 : t == "vector" ? 1 : t == "lcd" ? 2 : t == "svg" ? 3 : -1;
            int rot = attr(l, "rotate").toInt();
            g.orientation = (rot == 90 || rot == 270) ? 1 : 0;
            QString w = attr(l, "width"), h = attr(l, "height");
            if (!w.isEmpty()) g.resolution = w + "x" + h;
            g.refresh = attr(l, "refresh");
        } else if (l.startsWith(u"<input ")) {
            g.players = attr(l, "players").toShort();
            hasCoins = !attr(l, "coins").isEmpty();
        } else if (l.startsWith(u"<control ")) {
            QString t = attr(l, "type"), ways = attr(l, "ways");
            if (t == "joy") // joy8way, joy4way, vjoy2way (ways="vertical2"), ...
                t = ways.startsWith("vertical") ? "vjoy" + ways.mid(8) + "way" : "joy" + ways + "way";
            ctrls.insert(t);
            int b = attr(l, "buttons").toInt();
            if (b > g.buttons) g.buttons = qint16(b);
        } else if (l.startsWith(u"<driver ")) {
            g.driverStatus = status(attr(l, "status"));
            g.emuStatus = status(attr(l, "emulation"));
            g.colorStatus = status(attr(l, "color"));
            g.soundStatus = status(attr(l, "sound"));
            g.graphicStatus = status(attr(l, "graphic"));
            g.protectionStatus = status(attr(l, "protection"));
            g.cocktailStatus = status(attr(l, "cocktail"));
            if (g.cocktailStatus == -1) {
                // ParseDriverStatusInfo: "cocktail" in the title's brackets: the driver's status
                QString t = g.title.toLower();
                if (t.contains("cocktail)") || t.contains("(cocktail") || t.contains("cocktail,") || t.contains(", cocktail"))
                    g.cocktailStatus = g.driverStatus;
            }
            QString ss = attr(l, "savestate");
            g.saveState = ss == "supported" ? 1 : ss == "unsupported" ? 0 : -1;
        } else if (l.startsWith(u"<feature ")) {
            // ParseFeatureInfo: since MAME 0.190 color, sound and graphics are <feature> lines
            // (type="palette|sound|graphics", status or overall="imperfect|unemulated"), as are
            // protection and the local network (lan); the old <driver color=… sound=… graphic=…
            // protection=…> wins when it's there.
            QString t = attr(l, "type");
            qint8* field = t == "sound" ? &g.soundStatus : t == "graphics" ? &g.graphicStatus
                         : t == "palette" ? &g.colorStatus : t == "protection" ? &g.protectionStatus
                         : t == "lan" ? &g.lanStatus : nullptr;
            if (field && *field == -1) {
                QString st = attr(l, "status");
                if (st.isEmpty()) st = attr(l, "overall");
                if (st == "unemulated") st = "preliminary";
                *field = status(st);
            }
        } else if (l.startsWith(u"<biosset ") && g.kind == SetKind::Bios) {
            if (this->biosSets.isEmpty() || this->biosSets.last().bios != g.name) this->biosSets.push_back({g.name, g.title, {}, {}});
            QString set = attr(l, "name");
            this->biosSets.last().sets.push_back({set, attr(l, "description")});
            if (attr(l, "default") == "yes") this->biosSets.last().defaultSet = set;
        } else if (l.startsWith(u"<sample ")) {
            g.audioType = 2;
        } else if (l.startsWith(u"<softwarelist ")) {
            // Arcade boards with cartridge lists (IsMultiSlotSoftwareList) stay arcade, except vectrex.
            QString sl = attr(l, "name");
            static const QStringList multiSlot = {"neogeo", "megatech", "stv", "vectrex"};
            if ((!multiSlot.contains(sl) || g.name == "vectrex") && !softlists.contains(sl)) softlists << sl;
        } else if (l.startsWith(u"<chip ") && attr(l, "type") == "audio" && attr(l, "name").contains("Discrete")) {
            if (g.audioType == 0) g.audioType = 1;
        } else if (g.mainCpu.isEmpty() && l.startsWith(u"<chip ") && attr(l, "type") == "cpu") {
            const QString tag = attr(l, "tag"); // ParseMainCPUInfo: "maincpu" (":maincpu" in older MAMEs)
            if (tag == "maincpu" || tag == ":maincpu") g.mainCpu = attr(l, "name");
        }
    }
    if (list.isEmpty()) return false;
    // romof points at the parent for clones; only keep it as "bios" when it is a BIOS set.
    QHash<QString, QString> romof;
    for (const auto& m : list) romof.insert(m.name, m.bios);
    for (auto& m : list) {
        QString b = m.bios;
        if (!b.isEmpty() && b != m.cloneOf && !biosSets.contains(b)) m.romOf = b; // a device's ROMs from another's
        int guard = 0;
        while (!b.isEmpty() && !biosSets.contains(b) && guard++ < 4) b = romof.value(b);
        m.bios = biosSets.contains(b) ? b : QString();
    }
    // ROM flags (the original's eROMInfo, eHaveGameROMs, eDeviceSets, eROMsAllNoDump and
    // eCRC32Duplicates, from its ROMs database): any ROM, the game's own ROMs (not its BIOS's),
    // devices with ROMs, all "no dump", and a CRC32 that another ROM with other data has.
    {
        QSet<QString> devicesWithRoms;
        for (const auto& m : list)
            if (m.kind == SetKind::Device) devicesWithRoms.insert(m.name); // romless devices were left out
        QHash<QString, QSet<QString>> biosCrcs, shaOfCrc;
        for (auto it = romsOf.cbegin(); it != romsOf.cend(); ++it)
            for (const Rom& r : it->roms) {
                if (biosSets.contains(it.key()) && !r.crc.isEmpty()) biosCrcs[it.key()].insert(r.crc);
                if (!r.crc.isEmpty() && !r.sha1.isEmpty()) shaOfCrc[r.crc].insert(r.sha1);
            }
        for (auto& m : list) {
            const Roms r = romsOf.value(m.name);
            quint8 f = MameGame::RomsKnown;
            bool devRoms = false;
            for (const QString& d : r.devices) devRoms |= devicesWithRoms.contains(d);
            if (devRoms) f |= MameGame::DeviceRoms;
            if (!r.roms.isEmpty() || devRoms) f |= MameGame::AnyRoms;
            const QSet<QString> bios = biosCrcs.value(m.bios);
            bool own = m.kind == SetKind::Bios || m.kind == SetKind::Device;
            for (const Rom& rom : r.roms) {
                own |= rom.crc.isEmpty() || !bios.contains(rom.crc) || m.name == m.bios;
                if (!rom.crc.isEmpty() && shaOfCrc.value(rom.crc).size() > 1) f |= MameGame::CrcCollision;
            }
            if (own) f |= MameGame::GameRoms;
            if (!r.hashed) f |= MameGame::AllNoDump;
            f |= MameGame::OwnFilesKnown;
            // a clone's file without merge="..." that its parent or BIOS has all the same (same CRC,
            // or SHA-1 for a disk) is no file of its own either: MAME finds it in the parent's set
            bool ownFiles = r.ownFiles;
            if (ownFiles && (!m.cloneOf.isEmpty() || !m.romOf.isEmpty())) {
                QSet<QString> shared;
                for (const QString& from : {m.cloneOf, m.bios, romof.value(m.cloneOf), m.romOf})
                    if (!from.isEmpty())
                        for (const Rom& pr : romsOf.value(from).roms) {
                            if (!pr.crc.isEmpty()) shared.insert(pr.crc);
                            if (!pr.sha1.isEmpty()) shared.insert("sha1:" + pr.sha1);
                        }
                ownFiles = false;
                for (const QString& u : r.unmerged) ownFiles |= !shared.contains(u);
            }
            if (ownFiles) f |= MameGame::OwnFiles;
            m.romFlags = f;
        }
    }
    // Neo-Geo games share one [neogeo] section (the first one found).
    bool neoGeoAdded = false;
    for (auto& [m, c] : withCards) {
        QString b = m.bios;
        int guard = 0;
        while (!b.isEmpty() && !biosSets.contains(b) && guard++ < 4) b = romof.value(b);
        bool neo = MemoryCards::isNeoGeo(biosSets.contains(b) ? b : QString(), m.driver);
        if (neo && neoGeoAdded) continue;
        neoGeoAdded |= neo;
        c.section = neo ? QStringLiteral("neogeo") : m.name;
        memoryCards.append(c);
    }
    // Same layout as EmuLoader's lists: [devices], [bios], then each parent followed by its clones.
    QHash<QString, int> idx;
    for (int i = 0; i < list.size(); ++i) idx.insert(list[i].name, i);
    QVector<MameGame> ordered;
    ordered.reserve(list.size());
    QHash<QString, QVector<int>> clonesOf;
    for (int i = 0; i < list.size(); ++i)
        if (list[i].kind == SetKind::Clone && idx.contains(list[i].cloneOf)) clonesOf[list[i].cloneOf].push_back(i);
    for (auto& m : list) if (m.kind == SetKind::Device) { m.section = "devices"; ordered.push_back(m); }
    for (auto& m : list) if (m.kind == SetKind::Bios) { m.section = "bios"; ordered.push_back(m); }
    QVector<int> parents;
    for (int i = 0; i < list.size(); ++i)
        if ((list[i].kind == SetKind::Parent) || (list[i].kind == SetKind::Clone && !idx.contains(list[i].cloneOf)))
            parents.push_back(i);
    std::sort(parents.begin(), parents.end(), [&](int a, int b) { return list[a].name < list[b].name; });
    for (int p : parents) {
        list[p].section = list[p].name;
        ordered.push_back(list[p]);
        for (int c : clonesOf.value(list[p].name)) {
            list[c].section = list[p].name;
            ordered.push_back(list[c]);
        }
    }
    games_ = std::move(ordered);
    utf8_ = false;
    reindex();
    if (progress) progress(games_.size());
    return true;
}

void ElDatabase::quickScan(const QStringList& romFolders, const QSet<QString>* only) {
    QSet<QString> found;
    for (const QString& dir : romFolders) {
        QDir d(dir);
        for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString n = fi.fileName().toLower();
            if (fi.isDir()) found.insert(n);
            else if (n.endsWith(".zip") || n.endsWith(".7z")) found.insert(fi.completeBaseName().toLower());
        }
    }
    for (MameGame& g : games_) {
        if (g.isSoftware()) continue;
        if (only && !only->contains(g.name)) continue;
        if (g.scanMode == 2) continue; // "force available" stays as the user set it
        bool have = found.contains(g.name);
        // A clone (a BIOS's too: ng_mv1) with no files of its own (all its ROMs are its parent's)
        // has no zip in split sets: MAME runs it from the parent's, so it's there when the parent is.
        // So is a device whose ROMs are all another device's (qsound_hle, in qsound.zip).
        if (!have && g.romFlag(MameGame::OwnFilesKnown) && !g.romFlag(MameGame::OwnFiles)) {
            const QString from = !g.cloneOf.isEmpty() ? g.cloneOf : g.romOf;
            if (!from.isEmpty()) have = found.contains(from);
        }
        // In merged sets every clone is in its parent's zip: one a Full Scan found complete stays
        // so while the parent's file is there.
        if (!have && !g.cloneOf.isEmpty() && g.scanMode == 0 && g.setStatus == 0) have = found.contains(g.cloneOf);
        // A machine without any ROM or disk (breakout, a discrete game) needs no file at all.
        if (!have && g.romFlag(MameGame::RomsKnown) && !g.romFlag(MameGame::AnyRoms) && g.mediaType == 0) have = true;
        // A full audit's "set present but ROMs/CHDs missing" (1) can't be judged by a quick scan:
        // keep it while the set file is still there.
        if (have && g.setStatus == 1) continue;
        g.setStatus = have ? 0 : 2;
        if (g.scanMode != 0) g.scanMode = 1;
    }
}

} // namespace eld
