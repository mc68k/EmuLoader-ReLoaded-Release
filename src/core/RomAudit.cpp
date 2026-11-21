#include "RomAudit.h"

#include "TextFile.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace eld {

namespace {
// What MAME can't fix: not counted, not listed
bool ignorable(const QString& text) {
    return text.contains(QLatin1String("NO GOOD DUMP KNOWN")) || text.contains(QLatin1String("NEEDS REDUMP")) ||
           text.contains(QLatin1String("BUT OPTIONAL"));
}

RomAudit::Results parse(const QString& text, bool samples) {
    static const QRegularExpression summary(QStringLiteral(R"(^(romset|sampleset)\s+(\S+)\s+(?:\[(\S+)\]\s+)?(.*)$)"));
    static const QRegularExpression detail(QStringLiteral(R"(^(\S+)\s*:\s+(.+?)(?: \((\d+) bytes\))? - (.+)$)"));
    static const QRegularExpression device(QStringLiteral(R"(^NOT FOUND \((\S+)\)$)"));
    RomAudit::Results out;
    QVector<RomAudit::File> pending;
    bool skipHashes = false; // the EXPECTED / FOUND lines of a problem left out
    for (QString line : text.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        const QString t = line.trimmed();
        if (t.isEmpty()) continue;
        if (t.startsWith(QLatin1String("EXPECTED:")) || t.startsWith(QLatin1String("FOUND:"))) {
            if (!skipHashes && !pending.isEmpty() && pending.last().problem == RomAudit::BadChecksum) {
                const QString v = t.mid(t.indexOf(':') + 1).trimmed();
                (t.startsWith('E') ? pending.last().expected : pending.last().found) = v;
            }
            continue;
        }
        if (const auto m = summary.match(t); m.hasMatch() && m.captured(1) == (samples ? "sampleset" : "romset")) {
            RomAudit::Set s;
            s.key = m.captured(2);
            s.parent = m.captured(3);
            const QString said = m.captured(4);
            const qint8 status = said.startsWith(QLatin1String("is bad")) ? RomAudit::Bad : RomAudit::Good;
            // "is good", "is best available", "has no roms!": nothing to put right
            (samples ? s.samples : s.roms) = (status == RomAudit::Bad || !pending.isEmpty()) ? RomAudit::Bad : RomAudit::Good;
            s.files = pending;
            pending.clear();
            out.insert(s.key, s);
            continue;
        }
        if (const auto m = detail.match(line); m.hasMatch()) {
            QString what = m.captured(4).trimmed();
            skipHashes = ignorable(what);
            if (skipHashes) continue;
            RomAudit::File f;
            f.name = m.captured(2).trimmed();
            f.size = m.captured(3).isEmpty() ? -1 : m.captured(3).toLongLong();
            f.kind = samples ? RomAudit::Sample : (f.size >= 0 ? RomAudit::Rom : RomAudit::Disk);
            if (what.endsWith(':')) what.chop(1);
            f.text = what;
            if (what.startsWith(QLatin1String("NOT FOUND"))) {
                f.problem = RomAudit::NotFound;
                if (const auto d = device.match(what); d.hasMatch()) f.device = d.captured(1);
            } else if (what.startsWith(QLatin1String("INCORRECT CHECKSUM"))) {
                f.problem = RomAudit::BadChecksum;
            } else if (what.startsWith(QLatin1String("INCORRECT LENGTH"))) {
                f.problem = RomAudit::BadLength;
            } else {
                f.problem = RomAudit::Other;
            }
            pending << f;
        }
        // anything else ("12 romsets found, 10 were OK.") says nothing about one set
    }
    return out;
}

QString clean(QString s) { return s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' '); }
} // namespace

bool RomAudit::Set::hasDisks() const {
    for (const File& f : files)
        if (f.kind == Disk) return true;
    return false;
}

RomAudit::Results RomAudit::parseRoms(const QString& text) { return parse(text, false); }
RomAudit::Results RomAudit::parseSamples(const QString& text) { return parse(text, true); }

int RomAudit::setStatus(const Set& s, bool chds) {
    if (s.roms == NotChecked) return -1;
    if (s.roms == SetNotFound) return 2;
    bool bad = false, any = false;
    for (const File& f : s.files) {
        if (f.kind == Sample) continue;
        any = true;
        if (f.kind == Rom || chds) bad = true;
    }
    if (s.roms == Bad && !any) bad = true; // MAME said bad without saying why
    return bad ? 1 : 0;
}

QString RomAudit::file(const QString& gamesFolder, const QString& systemBase) {
    return QDir(gamesFolder).filePath(systemBase + "_audit.txt");
}

// S <tab> key <tab> parent <tab> roms <tab> samples
// F <tab> key <tab> kind <tab> name <tab> size <tab> problem <tab> text <tab> device <tab> expected <tab> found
RomAudit::Results RomAudit::load(const QString& file) {
    Results out;
    QString text;
    if (!readTextFile(file, text)) return out;
    for (QString line : text.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        const QStringList p = line.split('\t');
        if (p.size() >= 5 && p[0] == QLatin1String("S")) {
            Set& s = out[p[1]];
            s.key = p[1];
            s.parent = p[2];
            s.roms = qint8(p[3].toInt());
            s.samples = qint8(p[4].toInt());
        } else if (p.size() >= 10 && p[0] == QLatin1String("F") && out.contains(p[1])) {
            File f;
            f.kind = Kind(qBound(0, p[2].toInt(), 2));
            f.name = p[3];
            f.size = p[4].isEmpty() ? -1 : p[4].toLongLong();
            f.problem = Problem(qBound(0, p[5].toInt(), 3));
            f.text = p[6];
            f.device = p[7];
            f.expected = p[8];
            f.found = p[9];
            if (p.size() >= 14) {
                f.chdIssue = RomAudit::File::ChdIssue(qBound(0, p[10].toInt(), 3));
                f.chdFile = p[11];
                f.chdParent = p[12];
                f.chdParentSha1 = p[13];
            }
            out[p[1]].files << f;
        }
    }
    return out;
}

bool RomAudit::save(const QString& file, const Results& results) {
    QStringList keys = results.keys();
    keys.sort();
    QString out = QStringLiteral("# EmuLoader ReLoaded ROM audit (Scan Games, Full Scan): S key parent roms samples / F key kind name size problem text device expected found chdissue chdfile chdparent chdparentsha1\r\n");
    for (const QString& k : keys) {
        const Set& s = results[k];
        // joined, not .arg()'d: a "%1" in a name stays as it is
        out += QStringList{"S", clean(s.key), clean(s.parent), QString::number(s.roms), QString::number(s.samples)}.join('\t') + "\r\n";
        for (const File& f : s.files)
            out += QStringList{"F", clean(s.key), QString::number(int(f.kind)), clean(f.name), f.size < 0 ? QString() : QString::number(f.size),
                               QString::number(int(f.problem)), clean(f.text), clean(f.device), clean(f.expected), clean(f.found),
                               QString::number(int(f.chdIssue)), clean(f.chdFile), clean(f.chdParent), clean(f.chdParentSha1)}
                       .join('\t') + "\r\n";
    }
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, out, true);
}

void RomAudit::merge(Results& into, const Results& fresh) {
    for (auto it = fresh.cbegin(); it != fresh.cend(); ++it) {
        Set s = it.value();
        const auto old = into.constFind(it.key());
        if (old != into.cend()) {
            if (s.samples == NotChecked && old->samples != NotChecked) { // keep the samples' result
                s.samples = old->samples;
                for (const File& f : old->files)
                    if (f.kind == Sample) s.files << f;
            }
            if (s.roms == NotChecked && old->roms != NotChecked) { // a samples-only result: keep the ROMs'
                s.roms = old->roms;
                if (s.parent.isEmpty()) s.parent = old->parent;
                for (const File& f : old->files)
                    if (f.kind != Sample) s.files << f;
            }
        }
        into.insert(it.key(), s);
    }
}

} // namespace eld
