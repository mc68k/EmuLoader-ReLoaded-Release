// ImageIndex - every picture of one image category, found in one pass (the Images Manager):
// the files in the category's folders and their subfolders, and the files inside its zip files
// (snap.zip, flyers.zip ...), each with the game its name belongs to.
//
// A picture's name (ImageFinder's naming), relative to a category folder or the zip, with an
// optional <category folder>\ in front:
//   <name>.png             the game's picture (the first one)
//   <name>0000.png ...     its other pictures (EmuLoader naming, 0000 to 0097)
//   <name>\0000.png ...    snapshots in MAME's own naming (0000 is the first one)
//   <list>\<name>.png ...  a software list game's (game "<list>/<name>"), the same three ways;
//                          in <list>.zip and <list>\<list>.zip, <name>.png ...
// (<name>\0000.png is the machine's snapshot: MAME saves a software's as its machine's)
// .jpg too for the categories that aren't PNG only. Anything else belongs to no game.
#pragma once

#include "ImageFinder.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace eld {

class ZipEditor;

struct ImageEntry {
    QString file;        // the picture, or the zip it's in
    QString entry;       // its name inside the zip ("" when it's a file)
    QString stored;      // ... as the zip has it now (entry is the new name of a renamed one)
    QString game;        // lowercase: the game its name belongs to ("" when none fits)
    bool first = false;  // the game's first picture (what Scan Missing Images looks for)
    bool picture = true; // PNG or JPEG inside (checked only when asked: build(..., true))
    qint64 size = 0;
    QDateTime modified;
    bool zipped() const { return !entry.isEmpty(); }
    QString name() const; // the file name, or the name inside the zip
};

class ImageIndex {
public:
    // progress(done, total) returns false to stop. checkContent: read each picture's first
    // bytes to see that it is a PNG or a JPEG (entry.picture). edits: changes to zip files not
    // written yet, shown as if they were.
    bool build(const ImageFinder& finder, int category, bool checkContent = false,
               const std::function<bool(int, int)>& progress = {}, const QHash<QString, ZipEditor>* edits = nullptr);

    int category() const { return category_; }
    bool isBuilt() const { return built_; }
    const QVector<ImageEntry>& entries() const { return entries_; }
    bool hasFirst(const QString& game) const { return first_.contains(game.toLower()); }
    QList<int> ofGame(const QString& game) const { return byGame_.values(game.toLower()); }
    // The pictures that are no picture of any of `games` (lowercase), or aren't PNG / JPEG
    QList<int> unused(const QSet<QString>& games) const;
    QStringList folders() const { return folders_; }
    QStringList zips() const { return zips_; }

    // The game a picture's name belongs to (see above); "" when none
    static QString gameOf(int category, QString relative, bool* first = nullptr);
    // PNG or JPEG (an icon for .ico), by the file's first bytes
    static bool isPicture(const QByteArray& head, const QString& ext = {});

private:
    void add(const ImageEntry& e);
    int category_ = -1;
    bool built_ = false;
    QVector<ImageEntry> entries_;
    QSet<QString> first_;
    QMultiHash<QString, int> byGame_;
    QStringList folders_, zips_;
};

} // namespace eld
