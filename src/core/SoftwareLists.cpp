#include "SoftwareLists.h"
#include "Frontend.h"
#include "MameIni.h"
#include "TextFile.h"

#include "miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace eld {

// ------------------------------------------------------------------ places

QString SoftwareLists::gamesFolder(const QString& userDir, const QString& systemBase) {
    return QDir(userDir).filePath("arcade/" + systemBase + "_softwarelist_games/");
}

QString SoftwareLists::requirementFile(const QString& machinesFolder, const QString& systemBase) {
    return QDir(machinesFolder).filePath(systemBase + ".elsoftlistreq");
}

QString SoftwareLists::excludeFile(const QString& userDir, const QString& systemBase) {
    return QDir(userDir).filePath("arcade/" + systemBase + "_softlist_exclude.txt");
}

QString SoftwareLists::machinesFile(const QString& userDir, const QString& systemBase) {
    return QDir(userDir).filePath("arcade/" + systemBase + "_machines_usercustom.ini");
}

QString SoftwareLists::hashFolder(const QString& mameIni, const QString& mameFolder) {
    MameIniFile f;
    f.load(mameIni);
    QStringList paths = splitPathList(f.get("hashpath", "hash"));
    return resolvePath(mameFolder, paths.isEmpty() ? QStringLiteral("hash") : paths.first());
}

QString SoftwareLists::mediaOption(const QString& p) {
    if (p == "cart" || p == "flop" || p == "cass" || p == "hard") return p + "1";
    if (p == "card" || p == "card1" || p == "rom" || p == "rom1" || p == "socket") return "cart1";
    if (p == "cpm1" || p.contains("disk") || p.contains("flop")) return "flop1";
    if (p.contains("cass")) return "cass1";
    if (p == "hdd" || p == "hdd1") return "hard1";
    if (p == "disc" || p.contains("cdrom")) return "cdrom";
    return p;
}

SoftwareLists::ListInfo SoftwareLists::listInfo(const QString& xmlFile) {
    ListInfo out;
    out.name = QFileInfo(xmlFile).completeBaseName();
    out.description = out.name;
    const QString n = out.name.toLower();
    if (n.contains("_cart")) out.media = 1;
    else if (n.contains("_cdrom")) out.media = 2;
    else if (n.contains("_flop") || n.contains("_qd")) out.media = 3;
    else if (n.contains("_cass")) out.media = 4;
    else if (n.contains("_hdd")) out.media = 5;
    const bool fromName = out.media != 0;
    QFile f(xmlFile);
    if (!f.open(QIODevice::ReadOnly)) return out;
    QXmlStreamReader x(&f);
    bool title = false;
    while (!x.atEnd() && !(title && (fromName || out.media))) {
        x.readNext();
        if (!x.isStartElement()) continue;
        if (x.name() == u"softwarelist" && !title) {
            const QString d = x.attributes().value("description").toString().simplified();
            if (!d.isEmpty()) out.description = d;
            title = true;
        } else if (x.name() == u"part" && !fromName) {
            const QString p = mediaOption(x.attributes().value("name").toString());
            out.media = p == "cart1" ? 1 : (p == "cdrom" || p == "cdrom1") ? 2 : p == "flop1" ? 3
                      : (p == "cass1" || p == "tape") ? 4 : p == "hard1" ? 5 : 1; // "quik", "memc" ...
        }
    }
    if (out.name.compare("vgmplay", Qt::CaseInsensitive) == 0) out.media = 6;
    return out;
}

QVector<SoftwareLists::ListInfo> SoftwareLists::hashLists(const QString& hashFolder) {
    QVector<ListInfo> out;
    for (const QFileInfo& fi : QDir(hashFolder).entryInfoList({"*.xml"}, QDir::Files, QDir::Name)) {
        ListInfo i = listInfo(fi.absoluteFilePath());
        if (i.media) out.push_back(i);
    }
    return out;
}

// --------------------------------------------------------- requirements

bool SoftwareLists::Requirements::load(const QString& file) {
    lists.clear();
    QString text;
    if (!readTextFile(file, text)) return false;
    QString section;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }
        int eq = line.indexOf('=');
        if (eq > 0 && !section.isEmpty()) lists[section].insert(line.left(eq), line.mid(eq + 1));
    }
    return true;
}

bool SoftwareLists::Requirements::save(const QString& file) const {
    QString out;
    for (auto l = lists.cbegin(); l != lists.cend(); ++l) {
        if (l.value().isEmpty()) continue;
        out += "[" + l.key() + "]\r\n";
        for (auto g = l.value().cbegin(); g != l.value().cend(); ++g) out += g.key() + "=" + g.value() + "\r\n";
        out += "\r\n";
    }
    if (out.isEmpty()) return !QFileInfo::exists(file) || QFile::remove(file);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, out, false);
}

// ------------------------------------------------------------- making

QString SoftwareLists::fileCrc32(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    mz_ulong crc = MZ_CRC32_INIT;
    while (!f.atEnd()) {
        QByteArray b = f.read(1 << 20);
        if (b.isEmpty()) break;
        crc = mz_crc32(crc, reinterpret_cast<const unsigned char*>(b.constData()), size_t(b.size()));
    }
    return QString::number(quint32(crc), 16).toUpper().rightJustified(8, '0');
}

static QString oneLine(const QString& s) { return s.simplified(); }

// <size> of a <rom>: decimal, or hex with "0x"; "" or "?" when unknown.
static qint64 romSize(const QString& s) {
    if (s.isEmpty() || s.startsWith('?')) return 0;
    bool ok = false;
    qint64 v = s.startsWith("0x", Qt::CaseInsensitive) ? s.mid(2).toLongLong(&ok, 16) : s.toLongLong(&ok);
    return ok ? v : 0;
}

namespace {
struct Soft {
    QString name, cloneOf, title, year, publisher, notes, altTitle, compat, usage, media, requirement;
    qint64 size = 0;
    int mediaType = 0, status = 0; // status: 0 good, 1 partial, 2 unsupported
};
}

bool SoftwareLists::create(const QString& xmlFile, const QString& list, const QString& folder, bool hbmame,
                           Requirements& req, QString* error) {
    QFile f(xmlFile);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QDir::toNativeSeparators(xmlFile) + ": " + f.errorString();
        return false;
    }
    QXmlStreamReader x(&f);
    QString description;
    QVector<Soft> games;
    Soft cur;
    bool inSoftware = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isEndElement() && x.name() == u"software") {
            if (inSoftware && !cur.name.isEmpty()) games.push_back(cur);
            inSoftware = false;
            continue;
        }
        if (!x.isStartElement()) continue;
        const auto n = x.name();
        const QXmlStreamAttributes a = x.attributes();
        if (n == u"softwarelist") {
            description = oneLine(a.value("description").toString());
        } else if (n == u"software") {
            cur = Soft{};
            inSoftware = true;
            cur.name = a.value("name").toString();
            cur.cloneOf = a.value("cloneof").toString();
            const auto sup = a.value("supported");
            cur.status = sup == u"no" ? 2 : sup == u"partial" ? 1 : 0;
        } else if (!inSoftware) {
            continue;
        } else if (n == u"description") {
            cur.title = oneLine(x.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (n == u"year") {
            cur.year = oneLine(x.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (n == u"publisher") {
            cur.publisher = oneLine(x.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (n == u"notes") {
            cur.notes = oneLine(x.readElementText(QXmlStreamReader::IncludeChildElements));
        } else if (n == u"info") {
            const auto in = a.value("name");
            if (in == u"alt_title") cur.altTitle = oneLine(a.value("value").toString());
            else if (in == u"usage") cur.usage = oneLine(a.value("value").toString());
        } else if (n == u"sharedfeat") {
            const auto sn = a.value("name");
            if (sn == u"compatibility") cur.compat = a.value("value").toString();
            else if (sn == u"requirement" && cur.requirement.isEmpty()) cur.requirement = a.value("value").toString();
        } else if (n == u"part") {
            if (cur.media.isEmpty()) cur.media = list == "vgmplay" ? QStringLiteral("quik") : mediaOption(a.value("name").toString());
        } else if (n == u"rom") {
            if (!a.value("name").isEmpty()) cur.size += romSize(a.value("size").toString());
        } else if (n == u"disk") {
            cur.mediaType = 1;
        }
    }
    if (x.hasError()) {
        if (error) *error = QString("%1: line %2: %3").arg(QDir::toNativeSeparators(xmlFile)).arg(x.lineNumber()).arg(x.errorString());
        return false;
    }
    f.close();

    // A section per parent (in the XML's order), with it and its clones.
    QStringList order;
    QHash<QString, QString> sections;
    for (const Soft& s : games)
        if (s.cloneOf.isEmpty() && !sections.contains(s.name)) {
            order << s.name;
            sections.insert(s.name, {});
        }
    auto tag = [](QString& line, const char* t, const QString& v) {
        if (!v.isEmpty()) line += QString("<%1>%2/>").arg(t, QString(v).replace('\n', ' ').replace('\r', ' '));
    };
    QString status;
    QMap<QString, QString>& reqs = req.lists[list];
    reqs.clear();
    for (const Soft& s : games) {
        QString line = s.name + "=" + QString::number(s.mediaType) + QString::number(s.status) + QString::number(s.status);
        tag(line, "title", s.title);
        tag(line, "alt_title", s.altTitle);
        tag(line, "year", s.year);
        tag(line, "publisher", s.publisher);
        tag(line, "notes", s.notes);
        tag(line, "cloneof", s.cloneOf);
        if (s.size) line += QString("<size>%1/>").arg(s.size);
        tag(line, "media", s.media);
        tag(line, "usage", s.usage);
        tag(line, "compat", s.compat);
        const QString sec = s.cloneOf.isEmpty() ? s.name : s.cloneOf;
        if (!sections.contains(sec)) order << sec;
        sections[sec] += line + "\r\n";
        int id = (hbmame ? 26 : 4) + (s.cloneOf.isEmpty() ? 0 : 1);
        status += s.name + "=" + QString::number(id).rightJustified(2, '0') + "210\r\n"; // missing until scanned
        // The requirement, as the original keeps it: "<list>:<software>".
        QString r = s.requirement;
        if (!r.isEmpty()) {
            if (list.compare("a2600_cass", Qt::CaseInsensitive) == 0) r = "a2600:" + r;
            else if (list.compare("segacd", Qt::CaseInsensitive) == 0) {
                if (r.compare("32x", Qt::CaseInsensitive) == 0) r.clear(); // run with a Sega CD with 32X machine
            } else {
                int p = r.indexOf(" -");
                if (p >= 0) r.replace(p, 2, ":");
            }
            if (!r.isEmpty()) reqs.insert(s.name, r);
        }
    }
    QString el = "[info_file]\r\ndescription=" + (description.isEmpty() ? list : description) + "\r\n" +
                 "xmlfile_datetime=" + QString::number(Frontend::fileDosDate(xmlFile)) + "\r\n" +
                 "xmlfile_crc32=" + fileCrc32(xmlFile) + "\r\n\r\n";
    for (const QString& sec : order) el += "[" + sec + "]\r\n" + sections.value(sec) + "\r\n";
    remove(folder, list);
    QDir().mkpath(folder);
    QDir d(folder);
    return writeTextFile(d.filePath(list + ".el"), el, true) && writeTextFile(d.filePath(list + ".elstatus"), status, false);
}

void SoftwareLists::remove(const QString& folder, const QString& list) {
    QDir d(folder);
    for (const char* ext : {".el", ".elrom", ".elstatus", ".miss"}) QFile::remove(d.filePath(list + ext));
}

static QHash<QString, QString> infoFile(const QString& elFile) {
    QHash<QString, QString> out;
    QString text;
    if (!readTextFile(elFile, text)) return out;
    bool in = false;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[')) {
            if (in) break;
            in = line == "[info_file]";
            continue;
        }
        int eq = line.indexOf('=');
        if (in && eq > 0) out.insert(line.left(eq), line.mid(eq + 1));
    }
    return out;
}

bool SoftwareLists::upToDate(const QString& xmlFile, const QString& folder, const QString& list) {
    QString made = infoFile(QDir(folder).filePath(list + ".el")).value("xmlfile_crc32");
    return !made.isEmpty() && made.compare(fileCrc32(xmlFile), Qt::CaseInsensitive) == 0;
}

QStringList SoftwareLists::made(const QString& folder) {
    QStringList out;
    for (const QFileInfo& fi : QDir(folder).entryInfoList({"*.el"}, QDir::Files, QDir::Name)) out << fi.completeBaseName();
    return out;
}

QStringList SoftwareLists::excluded(const QString& file) {
    QStringList out;
    QString text;
    if (!readTextFile(file, text)) return out;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (!line.isEmpty()) out << line;
    }
    return out;
}

bool SoftwareLists::saveExcluded(const QString& file, const QStringList& lists) {
    if (lists.isEmpty()) return !QFileInfo::exists(file) || QFile::remove(file);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, lists.join("\r\n") + "\r\n", false);
}

// ------------------------------------------------------------- loading

QVector<MameGame> SoftwareLists::load(const QString& folder, const QString& list) {
    QVector<MameGame> out;
    QDir d(folder);
    QString text;
    if (!readTextFile(d.filePath(list + ".el"), text)) return out;
    QString title = list;
    QString section;
    for (QStringView line : QStringView(text).split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u';')) continue;
        if (line.startsWith(u'[')) {
            section = line.mid(1, line.size() - 2).toString();
            continue;
        }
        int eq = line.indexOf(u'=');
        if (eq <= 0) continue;
        if (section == "info_file") {
            if (line.startsWith(u"description=")) title = line.mid(eq + 1).toString();
            continue;
        }
        QStringView v = line.mid(eq + 1);
        if (v.size() < 3) continue;
        MameGame g;
        g.name = line.left(eq).toString();
        g.softwareList = list;
        g.systemType = 1;
        g.section = section;
        g.mediaType = qint8(qMax(0, v[0].digitValue()));
        g.driverStatus = qint8(qMax(0, v[1].digitValue()));
        g.emuStatus = qint8(qMax(0, v[2].digitValue()));
        g.screenType = list == "vectrex" ? 1 : 0;
        g.orientation = list == "vectrex" ? 1 : 0;
        for (qsizetype p = 3; p < v.size();) {
            if (v[p] != u'<') { ++p; continue; }
            qsizetype gt = v.indexOf(u'>', p);
            if (gt < 0) break;
            qsizetype end = v.indexOf(u"/>", gt + 1);
            if (end < 0) break;
            const QStringView tag = v.mid(p + 1, gt - p - 1);
            const QString value = v.mid(gt + 1, end - gt - 1).toString();
            p = end + 2;
            if (tag == u"title") g.title = value;
            else if (tag == u"alt_title") g.altTitle = value;
            else if (tag == u"year") g.year = value;
            else if (tag == u"publisher") g.manufacturer = value;
            else if (tag == u"notes") g.notes = value;
            else if (tag == u"cloneof") g.cloneOf = value;
            else if (tag == u"size") g.size = value.toLongLong();
            else if (tag == u"media") g.media = value;
            else if (tag == u"usage") g.usage = value;
            else if (tag == u"compat") g.compat = value;
        }
        g.kind = g.cloneOf.isEmpty() ? SetKind::Parent : SetKind::Clone;
        out.push_back(std::move(g));
    }
    for (MameGame& g : out) {
        g.softwareTitle = title;
        g.category = title; // the original shows the list's description as the category
    }
    // .elstatus: name=IISMm (the ID, set status, scan mode, merged)
    QString st;
    if (readTextFile(d.filePath(list + ".elstatus"), st)) {
        QHash<QString, int> index;
        for (int i = 0; i < out.size(); ++i) index.insert(out[i].name, i);
        for (QStringView line : QStringView(st).split('\n')) {
            line = line.trimmed();
            int eq = line.indexOf(u'=');
            if (eq <= 0) continue;
            int i = index.value(line.left(eq).toString(), -1);
            QStringView v = line.mid(eq + 1);
            if (i < 0 || v.size() < 3) continue;
            MameGame& g = out[i];
            g.setStatus = qint8(v[2].digitValue());
            if (v.size() > 3) g.scanMode = qint8(v[3].digitValue());
            if (v.size() > 4) g.merged = v[4] == u'1';
        }
    }
    return out;
}

QVector<MameGame> SoftwareLists::loadAll(const QString& folder, const QStringList& excludedLists) {
    QVector<MameGame> out;
    const QSet<QString> skip(excludedLists.cbegin(), excludedLists.cend());
    for (const QString& list : made(folder)) {
        if (skip.contains(list)) continue;
        QVector<MameGame> games = load(folder, list);
        out.reserve(out.size() + games.size());
        for (MameGame& g : games) out.push_back(std::move(g));
    }
    return out;
}

bool SoftwareLists::saveStatus(const QString& folder, const QVector<MameGame>& games, bool hbmame) {
    QMap<QString, QString> files;
    for (const MameGame& g : games) {
        if (!g.isSoftware()) continue;
        const int id = (hbmame ? 26 : 4) + (g.kind == SetKind::Clone ? 1 : 0);
        files[g.softwareList] += QString("%1=%2%3%4%5\r\n")
                                     .arg(g.name)
                                     .arg(id, 2, 10, QChar('0'))
                                     .arg(g.setStatus)
                                     .arg(g.scanMode)
                                     .arg(g.merged ? 1 : 0);
    }
    bool ok = true;
    for (auto f = files.cbegin(); f != files.cend(); ++f)
        ok &= writeTextFile(QDir(folder).filePath(f.key() + ".elstatus"), f.value(), false);
    return ok;
}

void SoftwareLists::quickScan(QVector<MameGame>& games, const QStringList& romFolders, const QSet<QString>* only) {
    QHash<QString, QSet<QString>> found; // list -> what's in <rom folder>\<list>
    for (MameGame& g : games) {
        if (!g.isSoftware() || g.scanMode == 2) continue;
        if (only && !only->contains(g.key())) continue;
        auto it = found.find(g.softwareList);
        if (it == found.end()) {
            QSet<QString> names;
            for (const QString& dir : romFolders) {
                QDir d(QDir(dir).filePath(g.softwareList));
                if (!d.exists()) continue;
                for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                    const QString n = fi.fileName().toLower();
                    if (fi.isDir()) names.insert(n);
                    else if (n.endsWith(".zip") || n.endsWith(".7z")) names.insert(fi.completeBaseName().toLower());
                }
            }
            it = found.insert(g.softwareList, names);
        }
        const bool have = it->contains(g.name.toLower());
        if (have && g.setStatus == 1) continue; // a full audit's "files missing" stays while the set is there
        g.setStatus = have ? 0 : 2;
        if (g.scanMode != 0) g.scanMode = 1;
    }
}

// ------------------------------------------------------------- running

QString SoftwareLists::lastMachine(const QString& file, const QString& list) {
    QString text;
    if (!readTextFile(file, text)) return {};
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq) == list) return line.mid(eq + 1).trimmed();
    }
    return {};
}

bool SoftwareLists::setLastMachine(const QString& file, const QString& list, const QString& machine) {
    QString text;
    readTextFile(file, text);
    QStringList lines;
    bool done = false;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq) == list) {
            if (done) continue;
            line = list + "=" + machine;
            done = true;
        }
        lines << line;
    }
    if (!done) lines << list + "=" + machine;
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, lines.join("\r\n") + "\r\n", false);
}

QStringList SoftwareLists::requirementArgs(const Requirements& req, const QString& folder, const QString& list,
                                           const QString& game) {
    const QString value = req.value(list, game);
    if (value.isEmpty()) return {};
    QString reqList, reqSet = value;
    int colon = value.indexOf(':');
    if (colon >= 0) {
        reqList = value.left(colon);
        reqSet = value.mid(colon + 1);
    }
    QString option;
    if (!reqList.isEmpty()) {
        if (reqList.contains("_cart")) option = "-cart1";
        else if (reqList.contains("_flop")) option = "-flop1";
        else if (reqList.contains("_cass")) option = "-cass1";
        else if (QFileInfo::exists(QDir(folder).filePath(reqList + ".el"))) {
            // the required software's own media, from its list
            for (const MameGame& g : load(folder, reqList))
                if (g.name == reqSet) {
                    if (!g.media.isEmpty()) option = "-" + g.media;
                    break;
                }
        } else {
            option = "-" + reqList; // no such list: a device of the machine
        }
    } else if (list.compare("pcecd", Qt::CaseInsensitive) == 0) {
        option = "-cart1"; // PC Engine CD games: the System Card
    }
    QStringList out;
    if (!option.isEmpty()) out << option;
    if (!reqSet.isEmpty()) out << reqSet;
    return out;
}

QString SoftwareLists::customMachine(const QString& customParams) {
    const QString p = customParams.trimmed();
    if (p.isEmpty() || p.startsWith('-')) return {};
    int space = p.indexOf(' ');
    return space < 0 ? QString() : p.left(space);
}

QStringList SoftwareLists::commandLine(const QString& machine, const QString& customParams, const QStringList& requirement,
                                       const QString& media, const QString& game, QString* machineUsed) {
    const QString cp = customParams.trimmed();
    const QString own = customMachine(cp);
    QStringList args;
    if (cp.isEmpty()) args << machine;
    else if (!own.isEmpty()) args << QProcess::splitCommand(cp);
    else args << machine << QProcess::splitCommand(cp);
    if (machineUsed) *machineUsed = own.isEmpty() ? machine : own;
    args << requirement;
    if (cp.isEmpty()) {
        if (!media.isEmpty()) args << "-" + media;
        args << game;
        return args;
    }
    static const QRegularExpression tag("<game(name)?>", QRegularExpression::CaseInsensitiveOption);
    bool hasTag = false;
    for (const QString& a : args) hasTag |= a.contains(tag);
    if (hasTag) {
        for (QString& a : args) a.replace(tag, game);
        return args;
    }
    if (!media.isEmpty()) {
        // The media option is left out when the parameters already end with it ("-cart" for
        // "cart1"), as the game then follows it.
        QString base = media;
        if (base.size() > 1 && base.back() >= '1' && base.back() <= '9') base.chop(1);
        QString last;
        for (auto it = args.crbegin(); it != args.crend(); ++it)
            if (it->startsWith('-')) { last = *it; break; }
        if (last.isEmpty() || !last.startsWith("-" + base, Qt::CaseInsensitive)) args << "-" + media;
    }
    args << game;
    return args;
}

// ------------------------------------------------------------- files

QVector<SoftwareLists::File> SoftwareLists::files(const QString& xmlFile, const QString& game) {
    QVector<File> out;
    QFile f(xmlFile);
    if (!f.open(QIODevice::ReadOnly)) return out;
    QXmlStreamReader x(&f);
    bool in = false;
    QString part;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isEndElement() && x.name() == u"software" && in) break;
        if (!x.isStartElement()) continue;
        const QXmlStreamAttributes a = x.attributes();
        if (x.name() == u"software") {
            in = a.value("name") == game;
            continue;
        }
        if (!in) continue;
        if (x.name() == u"part") {
            part = a.value("name").toString();
        } else if (x.name() == u"rom" || x.name() == u"disk") {
            File file;
            file.name = a.value("name").toString();
            if (file.name.isEmpty()) continue; // a cartridge's own RAM, for instance
            file.disk = x.name() == u"disk";
            file.crc = a.value("crc").toString().toLower();
            file.sha1 = a.value("sha1").toString().toLower();
            file.status = a.value("status").toString();
            file.part = part;
            if (!file.disk) {
                const QString s = a.value("size").toString();
                file.size = s.isEmpty() || s.startsWith('?') ? -1 : romSize(s);
            }
            out.push_back(file);
        }
    }
    return out;
}

} // namespace eld
