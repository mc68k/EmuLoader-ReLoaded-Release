#include "MachineInfo.h"

#include "miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QXmlStreamReader>
#include <cstdio>

namespace eld {

QString MachineInfo::mainCpu() const {
    for (const Chip& c : chips) {
        if (c.type != "cpu") continue;
        if (c.clock <= 0) return c.name;
        double mhz = c.clock / 1e6;
        QString clock = mhz >= 1 ? QString::number(mhz, 'f', 6) : QString::number(c.clock / 1e3, 'f', 3);
        while (clock.contains('.') && (clock.endsWith('0') || clock.endsWith('.'))) clock.chop(1);
        return QString("%1 @ %2 %3").arg(c.name, clock, mhz >= 1 ? "MHz" : "kHz");
    }
    return {};
}

QHash<QString, MachineInfo> MachineInfo::parse(const QByteArray& xml) {
    QHash<QString, MachineInfo> out;
    QXmlStreamReader r(xml);
    MachineInfo m;
    bool in = false;
    QString text;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            auto a = r.attributes();
            const auto n = r.name();
            if (n == QLatin1String("machine") || n == QLatin1String("game")) {
                m = MachineInfo();
                in = true;
                m.name = a.value("name").toString();
                m.sourceFile = a.value("sourcefile").toString();
                m.cloneOf = a.value("cloneof").toString();
                m.romOf = a.value("romof").toString();
                m.sampleOf = a.value("sampleof").toString();
                m.isBios = a.value("isbios") == QLatin1String("yes");
                m.isDevice = a.value("isdevice") == QLatin1String("yes");
                m.mechanical = a.value("ismechanical") == QLatin1String("yes");
                m.runnable = a.value("runnable") != QLatin1String("no");
            } else if (!in) {
                continue;
            } else if (n == QLatin1String("description") || n == QLatin1String("year") || n == QLatin1String("manufacturer")) {
                QString t = r.readElementText();
                if (n == QLatin1String("description")) m.description = t;
                else if (n == QLatin1String("year")) m.year = t;
                else m.manufacturer = t;
            } else if (n == QLatin1String("rom") || n == QLatin1String("disk")) {
                Rom rom;
                rom.disk = n == QLatin1String("disk");
                rom.name = a.value("name").toString();
                rom.crc = a.value("crc").toString().toLower();
                rom.sha1 = a.value("sha1").toString().toLower();
                rom.merge = a.value("merge").toString();
                rom.region = a.value("region").toString();
                rom.status = a.value("status").toString();
                rom.bios = a.value("bios").toString();
                rom.size = a.value("size").toLongLong();
                rom.optional = a.value("optional") == QLatin1String("yes");
                m.roms.append(rom);
            } else if (n == QLatin1String("chip")) {
                m.chips.append({a.value("type").toString(), a.value("name").toString(), a.value("clock").toLongLong()});
            } else if (n == QLatin1String("sample")) {
                m.samples << a.value("name").toString();
            } else if (n == QLatin1String("device_ref")) {
                QString d = a.value("name").toString();
                if (!m.deviceRefs.contains(d)) m.deviceRefs << d;
            } else if (n == QLatin1String("softwarelist")) {
                m.softwareLists << a.value("name").toString();
            } else if (n == QLatin1String("sound")) {
                m.soundChannels = a.value("channels").toInt();
            } else if (n == QLatin1String("input")) {
                m.players = a.hasAttribute("players") ? a.value("players").toInt() : -1;
                m.coins = a.hasAttribute("coins") ? a.value("coins").toInt() : -1;
            } else if (n == QLatin1String("control")) {
                m.controls.append({a.value("type").toString(), a.value("buttons").toInt(), a.value("ways").toInt()});
            } else if (n == QLatin1String("driver")) {
                for (const auto& at : a) m.driver.insert(at.name().toString(), at.value().toString());
            } else if (n == QLatin1String("feature")) {
                QString st = a.value("status").toString();
                if (st.isEmpty()) st = a.value("overall").toString();
                m.features.insert(a.value("type").toString(), st);
            }
        } else if (r.isEndElement() && (r.name() == QLatin1String("machine") || r.name() == QLatin1String("game")) && in) {
            in = false;
            out.insert(m.name, m);
        }
    }
    return out;
}

// ------------------------------------------------------------------ set files

SetFiles::Found SetFiles::find(const QString& set) const {
    if (set.isEmpty()) return {};
    auto it = found_.constFind(set);
    if (it != found_.cend()) return *it;
    Found f;
    for (const QString& d : folders_) {
        QDir dir(d);
        if (dir.exists(set + ".zip")) { f = {dir.filePath(set + ".zip"), Found::Zip}; break; }
        if (dir.exists(set + ".7z")) { f = {dir.filePath(set + ".7z"), Found::SevenZip}; break; }
        if (QFileInfo(dir.filePath(set)).isDir()) { f = {dir.filePath(set), Found::Folder}; break; }
    }
    found_.insert(set, f);
    return f;
}

bool SetFiles::has(const QString& set, const QString& crc, const QString& name) const {
    Found f = find(set);
    switch (f.kind) {
    case Found::None: return false;
    case Found::SevenZip: return true;
    case Found::Folder: return QFileInfo::exists(QDir(f.path).filePath(name));
    case Found::Zip: break;
    }
    auto it = crcs_.find(f.path);
    if (it == crcs_.end()) {
        QSet<QString> crcs;
        mz_zip_archive za{};
        if (FILE* fp = std::fopen(QFile::encodeName(f.path).constData(), "rb")) {
            std::fseek(fp, 0, SEEK_END);
            long len = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            if (mz_zip_reader_init_cfile(&za, fp, len, 0)) {
                for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&za); ++i) {
                    mz_zip_archive_file_stat st;
                    if (mz_zip_reader_file_stat(&za, i, &st) && !st.m_is_directory)
                        crcs.insert(QString("%1").arg(st.m_crc32, 8, 16, QChar('0')));
                }
                mz_zip_reader_end(&za);
            }
            std::fclose(fp);
        }
        it = crcs_.insert(f.path, crcs);
    }
    return !crc.isEmpty() && it->contains(crc.toLower());
}

QString SetFiles::chd(const QString& set, const QString& disk) const {
    if (set.isEmpty()) return {};
    for (const QString& d : folders_) {
        QString p = QDir(d).filePath(set + "/" + disk + ".chd");
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

qint64 SetFiles::size(const QString& path) const {
    QFileInfo fi(path);
    if (!fi.isDir()) return fi.size();
    qint64 total = 0;
    for (const QFileInfo& e : QDir(path).entryInfoList(QDir::Files)) total += e.size();
    return total;
}

} // namespace eld
