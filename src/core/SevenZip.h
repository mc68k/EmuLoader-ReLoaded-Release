// SevenZip - a .7z archive read with the LZMA SDK's decoder (third_party/lzma, public domain):
// its files, and each file's bytes. Picture and icon archives (icons.7z, snap.7z ...) are read
// through ZipArchive, which opens a .7z with this.
//
// A .7z is usually "solid": many files compressed together in one block, so a file in the middle
// of a block can only be had by decompressing the block up to it. A block of up to 64 MB is
// decompressed whole into memory once and kept (the last one), so the next files from it are
// instant; a bigger one is decompressed as a stream each time, up to the file, without keeping it
// (that's slow: slow() says so, and EmuLoader offers to convert such an archive to a .zip).
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace eld {

class SevenZip {
public:
    SevenZip();
    ~SevenZip();
    SevenZip(const SevenZip&) = delete;
    SevenZip& operator=(const SevenZip&) = delete;

    bool open(const QString& file);
    void close();
    bool isOpen() const;
    QString error() const { return error_; }
    QString fileName() const { return file_; }

    struct Entry {
        QString name;     // as stored, "/" separators
        qint64 size = 0;
        QDateTime modified;
        bool dir = false;
    };
    const QVector<Entry>& entries() const { return entries_; }
    int indexOf(const QString& name) const; // without case; -1

    // A file's bytes (empty and error() set when it can't be read). One thread at a time.
    // keepGoing: asked as a file in a big block is decoded, false to stop (empty; the picture
    // isn't wanted any more: another game was selected). A kept block is always decoded whole.
    QByteArray read(int index, const std::function<bool()>& keepGoing = {});
    QByteArray readHead(int index, int bytes, const std::function<bool()>& keepGoing = {}); // the first bytes (a big block: no more decoded)

    // The biggest solid block (the unpacked size of a block holding more than one file)
    qint64 largestSolidBlock() const;
    // A block up to this size is decompressed once and kept; a solid one over it makes every
    // file slow to get (64 MB; a test can set a small size to try the big-block way)
    static qint64 keptBlock();
    static void setKeptBlock(qint64 bytes);
    bool slow() const { return largestSolidBlock() > keptBlock(); }

    // Every file in the archive's order, each block decompressed once (for converting it):
    // f(index, bytes) returns false to stop. Returns false on an error (error() says it) or a stop.
    bool forEach(const std::function<bool(int, const QByteArray&)>& f);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    QString file_, error_;
    QVector<Entry> entries_;
};

// A .7z made into a .zip beside it (the same name, .zip): every file checked, written under a
// temporary name and only renamed once it's complete. The pictures (.png, .jpg ...) are stored,
// the rest compressed. progress(done, total) returns false to stop (nothing is left behind).
// Returns "" or what went wrong.
QString convertSevenZipToZip(const QString& sevenZip, const QString& zip,
                             const std::function<bool(qint64, qint64)>& progress = {});

} // namespace eld
