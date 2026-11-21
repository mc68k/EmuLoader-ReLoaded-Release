// ZipArchive - a zip file kept open for reading many of its files (the MAMu_ icons' icons.zip,
// the Images Manager's snap.zip ...): its file names (lowercase, "/" separators) found at once,
// each file read by name. A .7z (icons.7z, snap.7z ...) is read the same way (SevenZip), for
// reading only.
//
// ZipEditor - changes to a zip file collected first and written at once (the Images Manager):
// files removed or renamed. apply() writes a new zip next to it, copying the files that stay as
// they are (still compressed, so it's quick), and only replaces the old zip once the new one has
// been checked; the removed files are extracted first, so they can go to the Recycle Bin.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

namespace eld {

class ZipArchive {
public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    bool open(const QString& file);
    void close();
    bool isOpen() const;
    bool isSevenZip() const;               // a .7z (read only)
    bool slow() const;                     // a .7z whose files are slow to get (SevenZip::slow)
    static bool sevenZipName(const QString& file); // ends with .7z
    QString fileName() const { return file_; }
    bool contains(const QString& name) const { return index_.contains(name.toLower()); }
    // keepGoing: false when it's not wanted any more (asked before reading, and as a .7z's big
    // block is decoded): empty then
    QByteArray read(const QString& name, const std::function<bool()>& keepGoing = {}) const;
    int count() const { return int(index_.size()); }

    // The zips kept open for reading images (the image panels, the thumbnails), shared by every
    // thread: opened again when the file changed. release() closes one (or all): before a zip is
    // written again, as Windows doesn't let an open file be replaced.
    static std::shared_ptr<ZipArchive> shared(const QString& file);
    static void release(const QString& file = {});

    struct Entry {
        QString name;       // as stored, "/" separators
        qint64 size = 0;    // uncompressed
        QDateTime modified;
    };
    QVector<Entry> entries() const; // the files (not the folders), in the archive's order
    // The first `bytes` bytes of a file (to tell what it is without unpacking all of it)
    QByteArray readHead(const QString& name, int bytes) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    QString file_;
    QHash<QString, unsigned> index_;
    mutable QMutex mutex_; // one read at a time (the zip's file position is shared)
};

// Files added to a zip (Zip Images): written to a new zip with the zip's own files (copied as they
// are), then put in its place once checked. The files are stored, not compressed again (images
// are compressed already). A large zip (65,535 files or 4 GB) is a ZIP64 one.
struct ZipAdd {
    QString file;   // the file to add
    QString name;   // its name in the zip ("/" separators)
};
// replace: a file added takes the place of the zip's file of the same name (else the zip's stays
// and the file isn't added). progress(done, total) returns false to stop (the zip stays as it
// was). added: the files that went in. Returns "" or what went wrong.
QString addToZip(const QString& zip, const QVector<ZipAdd>& files, bool replace,
                 const std::function<bool(int, int)>& progress = {}, QStringList* added = nullptr);
// Unzip: the zip's files into `folder` (subfolders made). replace: over files already there (else
// they're skipped). written: the files written. Returns "" or what went wrong.
QString extractZip(const QString& zip, const QString& folder, bool replace,
                   const std::function<bool(int, int)>& progress = {}, QStringList* written = nullptr);

class ZipEditor {
public:
    ZipEditor() = default;
    explicit ZipEditor(const QString& zip) : zip_(zip) {}
    QString zip() const { return zip_; }

    void remove(const QString& entry);                     // the name as stored
    void rename(const QString& entry, const QString& to);  // a new name for a file
    void undo(const QString& entry);                       // forget a change
    bool isRemoved(const QString& entry) const { return removed_.contains(entry.toLower()); }
    QString renamedTo(const QString& entry) const;         // "" when not renamed
    int changes() const { return int(removed_.size() + renamed_.size()); }
    QStringList removed() const { return removed_.values(); }

    // Writes the changes. The removed files are extracted into `removedDir` (their names with
    // "/" made " "; listed in `extracted`). progress(done, total) returns false to stop (before
    // the old zip is touched). When every file is removed the zip itself is deleted.
    // Returns "" or what went wrong (the old zip then stays as it was).
    QString apply(const QString& removedDir, QStringList* extracted = nullptr,
                  const std::function<bool(int, int)>& progress = {});

private:
    QString zip_;
    QHash<QString, QString> removed_;                      // lowercase -> as stored
    QHash<QString, QPair<QString, QString>> renamed_;      // lowercase -> (as stored, new name)
};

} // namespace eld
