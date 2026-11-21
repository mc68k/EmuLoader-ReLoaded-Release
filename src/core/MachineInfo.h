// MachineInfo - what MAME says about one machine (mame -listxml <name>), for the Game Details
// window (TFormGameDetails): its ROMs and disks, devices, CPU, sound, samples, inputs and
// driver flags; and SetFiles, which looks for set files in the ROM folders and checks the ROMs
// in them by CRC (the original's quick check of a set's zip contents).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace eld {

struct MachineInfo {
    struct Rom {
        QString name, crc, sha1, merge, region, status; // status: "", "nodump", "baddump"
        qint64 size = 0;
        bool disk = false, optional = false;
        QString bios;      // the BIOS option it belongs to (<rom bios="...">)
    };
    struct Chip {
        QString type, name;
        qint64 clock = 0;
    };
    struct Control {
        QString type;
        int buttons = 0, ways = 0;
    };
    QString name, description, year, manufacturer, sourceFile, cloneOf, romOf, sampleOf;
    bool isBios = false, isDevice = false, mechanical = false, runnable = true;
    QList<Chip> chips;
    QList<Rom> roms;
    QStringList samples, deviceRefs, softwareLists;
    int players = -1, coins = -1, soundChannels = -1;
    QList<Control> controls;
    QHash<QString, QString> driver;    // <driver> attributes: status, emulation, cocktail, savestate ...
    QHash<QString, QString> features;  // <feature type="protection" status="imperfect"/> -> type -> status

    // "Z80 @ 3.072 MHz" - the first CPU
    QString mainCpu() const;

    // The machines in mame -listxml output (the one asked for and the devices it uses), by name.
    static QHash<QString, MachineInfo> parse(const QByteArray& xml);
};

// Set files in the ROM folders: <folder>/<set>.zip, .7z or a <set> folder.
class SetFiles {
public:
    explicit SetFiles(const QStringList& romFolders) : folders_(romFolders) {}
    struct Found {
        QString path;          // empty: not found
        enum Kind { None, Zip, SevenZip, Folder } kind = None;
    };
    Found find(const QString& set) const;
    // Whether the set has a file with this CRC (zip) or this name (folder). A .7z can't be
    // looked into here: its files count as found when the archive is there.
    bool has(const QString& set, const QString& crc, const QString& name) const;
    // <folder>/<set>/<disk>.chd
    QString chd(const QString& set, const QString& disk) const;
    qint64 size(const QString& path) const;

private:
    QStringList folders_;
    mutable QHash<QString, Found> found_;
    mutable QHash<QString, QSet<QString>> crcs_; // zip path -> CRCs (lower case hex)
};

} // namespace eld
