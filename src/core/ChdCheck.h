// ChdCheck - why MAME says a CHD that's there is "NOT FOUND". A Full Scan's disk MAME didn't find
// is looked for in the ROM folders (<folder>\<set>\<name>.chd, then the parent's folder); when the
// file is there, its header says why:
//   a delta CHD (its "parent SHA-1" isn't zero): MAME opens it only with the CHD it was made
//   from, and looks for that one among the CDs of the set's own parents. When it's another set's
//   (jojobajr2's cap-jjm-0 made from jojobar1's cap-jjm-1), MAME can't. The fix is a stand-alone
//   copy: chdman copy -i <the CHD> -ip <its parent CHD> -o <a new file>.
//   otherwise: the file is there but isn't the one this MAME wants (another SHA-1).
//
// The CHD header (big-endian; "MComprHD", then its length and version):
//   v5: SHA-1 at 84, parent SHA-1 at 104; v4: SHA-1 at 48, parent at 68; v3: SHA-1 at 80, parent at 100.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace eld {

class ChdCheck {
public:
    struct Header {
        bool ok = false;
        int version = 0;
        QString sha1, parentSha1; // hex, lower case; parentSha1 empty when it has no parent
        bool isDelta() const { return !parentSha1.isEmpty(); }
    };
    static Header read(const QString& file);

    // Where a set's disk is: <folder>\<set>\<name>.chd, then <folder>\<parent>\<name>.chd; "" when not there
    static QString find(const QStringList& romFolders, const QString& set, const QString& parent, const QString& name);

    // Every CHD one folder down in the ROM folders, by its SHA-1 (to find a delta's parent)
    static QHash<QString, QString> index(const QStringList& romFolders);

    // The chdman command line that makes a stand-alone copy next to it (<name>-full.chd)
    static QString copyCommand(const QString& chd, const QString& parent);
};

} // namespace eld
