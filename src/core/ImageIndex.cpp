#include "ImageIndex.h"

#include "ZipArchive.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace eld {

QString ImageEntry::name() const { return zipped() ? entry : QFileInfo(file).fileName(); }

bool ImageIndex::isPicture(const QByteArray& h, const QString& ext) {
    if (ext.compare("ico", Qt::CaseInsensitive) == 0) // an icon file (reserved 0, type 1)
        return h.size() >= 4 && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0;
    if (h.size() >= 8 && h.startsWith("\x89PNG\r\n\x1a\n")) return true;
    return h.size() >= 3 && uchar(h[0]) == 0xFF && uchar(h[1]) == 0xD8 && uchar(h[2]) == 0xFF;
}

QString ImageIndex::gameOf(int c, QString rel, bool* first) {
    if (first) *first = false;
    rel = QDir::fromNativeSeparators(rel).toLower();
    const QString sub = QString(ImageFinder::category(c).folder).toLower() + "/";
    if (rel.startsWith(sub)) rel = rel.mid(sub.size());
    const int dot = rel.lastIndexOf('.');
    if (dot <= 0) return {};
    const QString ext = rel.mid(dot + 1);
    if (!ImageFinder::extensions(c).contains(ext)) return {};
    const QString base = rel.left(dot);
    auto isIndex = [](const QString& s) {
        if (s.size() != 4) return false;
        for (QChar ch : s)
            if (!ch.isDigit()) return false;
        return true;
    };
    const QStringList parts = base.split('/');
    for (const QString& p : parts)
        if (p.isEmpty()) return {};
    const bool snapshot = c == 1 && ext == "png" && parts.size() >= 2 && isIndex(parts.last());
    if (parts.size() == 2 && snapshot) { // MAME's <name>\0000.png (snapshots)
        if (first) *first = parts[1] == "0000";
        return parts[0];
    }
    if (parts.size() == 3 && snapshot) { // a software list game's <list>\<name>\0000.png
        if (first) *first = parts[2] == "0000";
        return parts[0] + '/' + parts[1];
    }
    if (parts.size() > 2) return {};
    // <name>.png, or a software list game's <list>\<name>.png
    // <name>0000 could also be a game's own name ending in digits: the caller checks both
    if (first) *first = true;
    return base;
}

void ImageIndex::add(const ImageEntry& e) {
    const int i = entries_.size();
    entries_ << e;
    if (e.game.isEmpty()) return;
    byGame_.insert(e.game, i);
    if (e.first) first_.insert(e.game);
    // <name>0000.png: also one of <name>'s pictures
    const QString& g = e.game;
    if (g.size() - g.lastIndexOf('/') - 1 > 4) { // (a software list game's <list>/<name>0000.png too)
        const QString tail = g.right(4);
        bool digits = true;
        for (QChar ch : tail) digits &= ch.isDigit();
        if (digits && tail.toInt() < ImageFinder::MaxImages - 1) byGame_.insert(g.left(g.size() - 4), i);
    }
}

bool ImageIndex::build(const ImageFinder& finder, int c, bool checkContent, const std::function<bool(int, int)>& progress,
                       const QHash<QString, ZipEditor>* edits) {
    category_ = c;
    built_ = false;
    entries_.clear();
    first_.clear();
    byGame_.clear();
    folders_.clear();
    zips_ = finder.zipFiles(c);
    int done = 0;
    // often: a slow drive (or a virus scanner looking at each file) mustn't leave the window
    // without an update for long (the progress window draws at most ten times a second)
    auto tick = [&]() { return !progress || ++done % 10 != 0 || progress(done, 0); };

    // Only where pictures are looked for: the folder, its subfolders (<category folder>\,
    // MAME's <name>\ for snapshots) and the <category folder>\<name>\ ones; deeper folders, links
    // and junctions aren't followed (a folder set too high up would be read through otherwise).
    const QString catFolder = QString(ImageFinder::category(c).folder);
    QSet<QString> seen;
    auto subfolders = [](const QString& dir) {
        QStringList out;
        for (const QFileInfo& d : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden)) {
            if (d.isSymLink() || d.isJunction()) continue;
            out << d.absoluteFilePath();
        }
        return out;
    };
    for (const QString& dir : finder.folders(c)) {
        QDir root(dir);
        if (!root.exists()) continue;
        folders_ << root.absolutePath();
        QStringList dirs{root.absolutePath()};
        // (and a software list's folder: its games' <list>\<name>\0000.png snapshots)
        for (const QString& sub : subfolders(root.absolutePath())) {
            dirs << sub;
            const QString n = QFileInfo(sub).fileName();
            if (n.compare(catFolder, Qt::CaseInsensitive) == 0) {
                for (const QString& s2 : subfolders(sub)) {
                    dirs << s2;
                    if (c == 1 && finder.isSoftwareList(QFileInfo(s2).fileName())) dirs << subfolders(s2);
                }
            } else if (c == 1 && finder.isSoftwareList(n)) {
                dirs << subfolders(sub);
            }
        }
        for (const QString& d : dirs) {
            QDirIterator it(d, QDir::Files | QDir::Hidden);
            while (it.hasNext()) {
                it.next();
                if (!tick()) return false; // every file counts (the window stays up to date)
                const QFileInfo fi = it.fileInfo();
                const QString ext = fi.suffix().toLower();
                if (!ImageFinder::extensions(c).contains(ext)) continue; // not a picture of this category
                const QString key = fi.absoluteFilePath();
                if (seen.contains(key)) continue; // a folder listed twice, or inside another
                seen.insert(key);
                ImageEntry e;
                e.file = key;
                e.size = fi.size();
                e.modified = fi.lastModified();
                e.game = gameOf(c, root.relativeFilePath(key), &e.first);
                if (checkContent) {
                    if (e.size <= 0) {
                        e.picture = false;
                    } else {
                        QFile file(key);
                        e.picture = file.open(QFile::ReadOnly) && isPicture(file.read(16), ext);
                    }
                }
                add(e);
            }
        }
        if (progress && !progress(done, 0)) return false;
    }
    for (const QString& zip : zips_) {
        const QString prefix = finder.zipPrefix(zip);
        ZipArchive za;
        if (!za.open(zip)) continue;
        const ZipEditor* ed = nullptr;
        if (edits) {
            auto found = edits->constFind(zip);
            if (found != edits->constEnd()) ed = &found.value();
        }
        for (const ZipArchive::Entry& z : za.entries()) {
            if (!tick()) return false;
            if (ed && ed->isRemoved(z.name)) continue;
            ImageEntry e;
            e.file = zip;
            e.entry = e.stored = z.name;
            if (ed && !ed->renamedTo(z.name).isEmpty()) e.entry = ed->renamedTo(z.name);
            const QString ext = QFileInfo(e.entry).suffix().toLower();
            if (!ImageFinder::extensions(c).contains(ext)) continue;
            e.size = z.size;
            e.modified = z.modified;
            e.game = gameOf(c, prefix + e.entry, &e.first); // (nes.zip: smb.png is nes/smb's)
            if (checkContent) e.picture = z.size > 0 && isPicture(za.readHead(z.name, 16), ext);
            add(e);
        }
    }
    built_ = true;
    return true;
}

QList<int> ImageIndex::unused(const QSet<QString>& games) const {
    QList<int> out;
    for (int i = 0; i < entries_.size(); ++i) {
        const ImageEntry& e = entries_[i];
        bool used = !e.game.isEmpty() && games.contains(e.game);
        if (!used && e.game.size() > 4) { // <name>0000.png
            const QString tail = e.game.right(4);
            bool digits = true;
            for (QChar ch : tail) digits &= ch.isDigit();
            used = digits && tail.toInt() < ImageFinder::MaxImages - 1 && games.contains(e.game.left(e.game.size() - 4));
        }
        if (!used || !e.picture) out << i;
    }
    return out;
}

} // namespace eld
