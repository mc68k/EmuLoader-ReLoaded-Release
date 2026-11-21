#include "MamuIcons.h"
#include "SevenZip.h"

#include "ElDatabase.h"
#include "WinIni.h"

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>

namespace eld {

void MamuIcons::Options::load(const WinIni& ini) {
    enabled = ini.readBool("MAMu_", "EnableMAMu_Icons", enabled);
    parent = ini.readBool("MAMu_", "ShowParentIcons", parent);
    clone = ini.readBool("MAMu_", "ShowCloneIcons", clone);
    deviceSet = ini.readBool("MAMu_", "PopupEnableDeviceSetCustomIcon", deviceSet);
    bios = ini.readBool("MAMu_", "ShowGameBiosIcons", bios);
    driver = ini.readBool("MAMu_", "ShowGameDriverIcons", driver);
    forceBios = ini.readBool("MAMu_", "ForceGameBiosIcons", forceBios);
    forceDriver = ini.readBool("MAMu_", "ForceGameDriverIcons", forceDriver);
}

void MamuIcons::Options::save(WinIni& ini) const {
    ini.writeInteger("MAMu_", "EnableMAMu_Icons", enabled);
    ini.writeInteger("MAMu_", "ShowParentIcons", parent);
    ini.writeInteger("MAMu_", "ShowCloneIcons", clone);
    ini.writeInteger("MAMu_", "PopupEnableDeviceSetCustomIcon", deviceSet);
    ini.writeInteger("MAMu_", "ShowGameBiosIcons", bios);
    ini.writeInteger("MAMu_", "ShowGameDriverIcons", driver);
    ini.writeInteger("MAMu_", "ForceGameBiosIcons", forceBios);
    ini.writeInteger("MAMu_", "ForceGameDriverIcons", forceDriver);
}

void MamuIcons::setOptions(const Options& o) {
    o_ = o;
    found_.clear(); // which file a game uses depends on the options
}

void MamuIcons::setFolders(const QStringList& folders) {
    folders_ = folders;
    reload();
}

void MamuIcons::reload() {
    zips_.clear();
    preloaded_.clear();
    found_.clear();
    icons_.clear();
    for (const QString& f : folders_)
        for (const char* name : {"icons.zip", "icons.7z"}) { // (MAME reads both)
            const QString zip = QDir(f).filePath(name);
            if (!QFileInfo(zip).isFile()) continue;
            auto z = std::make_unique<ZipArchive>();
            if (!z->open(zip)) continue;
            if (z->slow()) { // every icon decompressed once now, rather than its block each time
                SevenZip sz;
                if (sz.open(zip))
                    sz.forEach([&](int i, const QByteArray& b) {
                        preloaded_.insert(zip + "|" + sz.entries()[i].name.toLower(), b);
                        return true;
                    });
            }
            zips_.push_back(std::move(z));
        }
}

int MamuIcons::zippedIcons() const {
    int n = 0;
    for (const auto& z : zips_) n += z->count();
    return n;
}

QString MamuIcons::find(const QString& nameIn) {
    if (nameIn.isEmpty()) return {};
    const QString name = nameIn.toLower();
    auto it = found_.find(name);
    if (it != found_.end()) return *it;
    QString where;
    for (const QString& f : folders_) { // ScanFoldersIcon: the folder, then its icons\ folder
        for (const QString& c : {QDir(f).filePath(name + ".ico"), QDir(f).filePath("icons/" + name + ".ico")})
            if (QFileInfo(c).isFile()) { where = c; break; }
        if (!where.isEmpty()) break;
    }
    if (where.isEmpty())
        for (const auto& z : zips_)
            if (z->contains(name + ".ico")) { where = z->fileName() + "|" + name + ".ico"; break; }
    found_.insert(name, where);
    return where;
}

QString MamuIcons::source(const MameGame& g) {
    if (!o_.enabled) return {};
    const QString& bios = g.bios;
    const QString driver = QFileInfo(g.driver).completeBaseName();
    QString r;
    if (o_.forceBios) r = find(bios);
    if (r.isEmpty() && o_.forceDriver) r = find(driver);
    if (r.isEmpty()) { // LoadGameIcon
        if (!g.cloneOf.isEmpty()) {
            if (o_.clone) r = find(g.name);
            if (r.isEmpty() && o_.parent) r = find(g.cloneOf);
        } else if (o_.parent) {
            r = find(g.name);
        }
    }
    if (r.isEmpty() && o_.deviceSet && g.kind == SetKind::Device) r = find("deviceset");
    if (r.isEmpty() && o_.bios && !o_.forceBios) r = find(bios);
    if (r.isEmpty() && o_.driver && !o_.forceDriver) r = find(driver);
    return r;
}

QIcon MamuIcons::load(const QString& where) {
    auto it = icons_.find(where);
    if (it != icons_.end()) return *it;
    QIcon icon;
    const int bar = where.lastIndexOf('|');
    if (bar > 0) { // in icons.zip
        const QString zip = where.left(bar), entry = where.mid(bar + 1);
        for (const auto& z : zips_)
            if (z->fileName() == zip) {
                // every size the .ico holds, as QIcon(file) has them
                const auto pre = preloaded_.constFind(zip + "|" + entry.toLower());
                QByteArray data = pre != preloaded_.cend() ? *pre : z->read(entry);
                QBuffer buf(&data);
                QImageReader r(&buf, "ICO");
                const int n = std::max(1, r.imageCount());
                for (int i = 0; i < n; ++i) {
                    if (i > 0 && !r.jumpToImage(i)) break;
                    const QImage img = r.read();
                    if (!img.isNull()) icon.addPixmap(QPixmap::fromImage(img));
                }
                break;
            }
    } else {
        icon = QIcon(where);
    }
    icons_.insert(where, icon);
    return icon;
}

QIcon MamuIcons::icon(const MameGame& g) {
    const QString where = source(g);
    return where.isEmpty() ? QIcon() : load(where);
}

} // namespace eld
