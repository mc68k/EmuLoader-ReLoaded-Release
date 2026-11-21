// RomAudit - Full Scan (Scan Games > Audit ROMs): MAME checks the sets itself. -verifyroms
// <names...> for the machines, -verifysoftlist <list> for a software list's games and
// -verifysamples <names...> for the audio samples; this reads what MAME says back.
//
// MAME's output, for each set it finds (a set with no file at all isn't mentioned):
//   pacman      : pacman.6e (4096 bytes) - NOT FOUND
//   puckman     : pm1-1.7f (4096 bytes) - INCORRECT CHECKSUM:
//   EXPECTED: CRC(c1e6ab10) SHA1(e87e059c...)
//      FOUND: CRC(00000000) SHA1(...)
//   kinst       : kinst - NOT FOUND                 (a disk: no size)
//   romset puckman [pacman] is bad                  (good, best available, bad)
//   sampleset galaga is bad
// The lines about a set come before its "romset" line. Problems MAME can't do anything about
// (NO GOOD DUMP KNOWN, NEEDS REDUMP, NOT FOUND BUT OPTIONAL) are left out.
//
// The results are kept in arcade\games\<system>_audit.txt (this version's own file: the
// original's mame.miss is written by its own CRC check, in another format).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class RomAudit {
public:
    enum Kind : quint8 { Rom, Disk, Sample };
    enum Problem : quint8 { NotFound, BadChecksum, BadLength, Other };
    struct File {
        Kind kind = Rom;
        QString name;          // "pacman.6e", "kinst", "fire.wav" (MAME gives samples without .wav)
        qint64 size = -1;      // ROMs only
        Problem problem = NotFound;
        QString text;          // MAME's own words, "NOT FOUND (neogeo)"
        QString device;        // the BIOS or device it belongs to ("neogeo"), when MAME says
        QString expected, found; // "CRC(...) SHA1(...)", for an incorrect checksum
        // A disk MAME didn't find but that's there (ChdCheck): why MAME can't use it
        enum ChdIssue : quint8 { NoIssue, DeltaParentElsewhere, DeltaParentMissing, OtherChd };
        ChdIssue chdIssue = NoIssue;
        QString chdFile;       // the CHD found
        QString chdParent;     // the CHD a delta CHD was made from, when it's found
        QString chdParentSha1; // its SHA-1
    };
    enum Status : qint8 { NotChecked = -1, Good = 0, Bad = 1, SetNotFound = 2 };
    struct Set {
        QString key;           // "pacman", "nes:smb" (a software list game)
        QString parent;        // what MAME gives in [ ]
        qint8 roms = NotChecked;    // ROMs and disks
        qint8 samples = NotChecked; // audio samples (only for the machines that play some)
        QVector<File> files;
        bool hasDisks() const;
    };
    using Results = QHash<QString, Set>;

    // -verifyroms / -verifysoftlist output; -verifysamples output
    static Results parseRoms(const QString& text);
    static Results parseSamples(const QString& text);

    // The games list status a set gets from its result: 0 complete, 1 incomplete (the set is
    // there, but files are missing or wrong), 2 not found. Disks count only with chds; a set
    // not checked (or a result without ROMs) gives -1.
    static int setStatus(const Set& s, bool chds);

    // The command lines, a batch of names at a time (MAME takes several)
    static QStringList verifyRomsArgs(const QStringList& names) { return QStringList{"-verifyroms"} + names; }
    static QStringList verifySamplesArgs(const QStringList& names) { return QStringList{"-verifysamples"} + names; }
    static QStringList verifySoftListArgs(const QString& list) { return {"-verifysoftlist", list}; }

    // arcade\games\<system>_audit.txt
    static QString file(const QString& gamesFolder, const QString& systemBase);
    static Results load(const QString& file);
    static bool save(const QString& file, const Results& results);
    // A new audit's results in place of the old ones of the same sets; the samples' result is
    // kept when the new audit didn't check them.
    static void merge(Results& into, const Results& fresh);
};

} // namespace eld
