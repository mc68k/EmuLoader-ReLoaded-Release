#include "SevenZip.h"

#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "Lzma2Dec.h"
#include "LzmaDec.h"

#include "miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimeZone>
#include <climits>
#include <atomic>
#include <mutex>

#ifdef Q_OS_WIN
#include <cstdio>
#endif

namespace eld {

namespace {
constexpr size_t kInBuf = size_t(1) << 18;
const ISzAlloc kAlloc = {SzAlloc, SzFree};
const ISzAlloc kAllocTemp = {SzAllocTemp, SzFreeTemp};

// The archive file, read by the decoder through Qt (Unicode names on Windows too)
struct QtStream {
    ISeekInStream vt;
    QFile* file;
};

SRes qtRead(ISeekInStreamPtr pp, void* buf, size_t* size) {
    auto* p = reinterpret_cast<QtStream*>(const_cast<ISeekInStream*>(pp));
    if (*size == 0) return SZ_OK;
    const qint64 n = p->file->read(static_cast<char*>(buf), qint64(*size));
    if (n < 0) {
        *size = 0;
        return SZ_ERROR_READ;
    }
    *size = size_t(n);
    return SZ_OK;
}

SRes qtSeek(ISeekInStreamPtr pp, Int64* pos, ESzSeek origin) {
    auto* p = reinterpret_cast<QtStream*>(const_cast<ISeekInStream*>(pp));
    const qint64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? p->file->pos() : p->file->size();
    const qint64 to = base + *pos;
    if (to < 0 || !p->file->seek(to)) return SZ_ERROR_READ;
    *pos = to;
    return SZ_OK;
}

QString sresText(SRes r) {
    switch (r) {
    case SZ_ERROR_UNSUPPORTED: return QStringLiteral("a compression method that isn't supported");
    case SZ_ERROR_MEM: return QStringLiteral("not enough memory");
    case SZ_ERROR_CRC: return QStringLiteral("a CRC error (the archive is damaged)");
    case SZ_ERROR_DATA: return QStringLiteral("damaged data");
    case SZ_ERROR_ARCHIVE: return QStringLiteral("not a valid .7z archive");
    case SZ_ERROR_NO_ARCHIVE: return QStringLiteral("not a .7z archive");
    case SZ_ERROR_INPUT_EOF: return QStringLiteral("the archive ends too soon");
    case SZ_ERROR_READ: return QStringLiteral("a read error");
    default: return QStringLiteral("error %1").arg(r);
    }
}

constexpr UInt32 kNone = 0xFFFFFFFF;
constexpr UInt32 kCopy = 0, kLzma = 0x30101, kLzma2 = 0x21;
} // namespace

struct SevenZip::Impl {
    QFile file;
    QtStream stream{};
    CLookToRead2 look{};
    CSzArEx db{};
    bool open = false;
    UInt32 blockIndex = kNone; // the block kept (SzArEx_Extract's cache)
    Byte* outBuffer = nullptr;
    size_t outBufferSize = 0;

    ~Impl() { close(); }
    void close() {
        if (outBuffer) ISzAlloc_Free(&kAlloc, outBuffer);
        outBuffer = nullptr;
        outBufferSize = 0;
        blockIndex = kNone;
        if (open) SzArEx_Free(&db, &kAlloc);
        open = false;
        if (look.buf) ISzAlloc_Free(&kAlloc, look.buf);
        look.buf = nullptr;
        file.close();
    }
    qint64 blockSize(UInt32 folder) const { return qint64(SzAr_GetFolderUnpackSize(&db.db, folder)); }
    // A block decompressed as a stream (one coder: LZMA, LZMA2 or stored), its bytes to sink in
    // order; sink returns false to stop
    QString decode(UInt32 folder, const std::function<bool(const char*, qint64)>& sink);
};

QString SevenZip::Impl::decode(UInt32 folder, const std::function<bool(const char*, qint64)>& sink) {
    const CSzAr& ar = db.db;
    const Byte* data = ar.CodersData + ar.FoCodersOffsets[folder];
    CSzData sd{data, ar.FoCodersOffsets[folder + 1] - ar.FoCodersOffsets[folder]};
    CSzFolder f;
    if (SzGetNextFolderItem(&f, &sd) != SZ_OK) return QStringLiteral("damaged block information");
    if (f.NumCoders != 1 || f.NumPackStreams != 1) return QStringLiteral("a filter that can only be read whole (BCJ, ...)");
    const CSzCoderInfo& c = f.Coders[0];
    const Byte* props = data + c.PropsOffset;
    const UInt64* packPos = ar.PackPositions + ar.FoStartPackStreamIndex[folder];
    qint64 packLeft = qint64(packPos[1] - packPos[0]);
    qint64 outLeft = blockSize(folder);
    if (!file.seek(qint64(db.dataPos + packPos[0]))) return QStringLiteral("a read error");
    look.pos = look.size = 0; // (the decoder's own buffer is out of step now)

    QByteArray in(int(kInBuf), Qt::Uninitialized), out(1 << 20, Qt::Uninitialized);
    if (c.MethodID == kCopy) {
        while (outLeft > 0) {
            const qint64 n = file.read(in.data(), std::min<qint64>(in.size(), outLeft));
            if (n <= 0) return QStringLiteral("the archive ends too soon");
            outLeft -= n;
            if (!sink(in.constData(), n)) return {};
        }
        return {};
    }
    if (c.MethodID != kLzma && c.MethodID != kLzma2) return QStringLiteral("a compression method that isn't supported");
    const bool two = c.MethodID == kLzma2;
    CLzmaDec lzma;
    CLzma2Dec lzma2;
    LzmaDec_CONSTRUCT(&lzma);
    Lzma2Dec_CONSTRUCT(&lzma2);
    SRes r = two ? (c.PropsSize == 1 ? Lzma2Dec_Allocate(&lzma2, props[0], &kAlloc) : SZ_ERROR_UNSUPPORTED)
                 : LzmaDec_Allocate(&lzma, props, c.PropsSize, &kAlloc);
    if (r != SZ_OK) return sresText(r);
    if (two) Lzma2Dec_Init(&lzma2);
    else LzmaDec_Init(&lzma);
    QString err;
    qint64 inPos = 0, inSize = 0;
    while (outLeft > 0) {
        if (inPos == inSize && packLeft > 0) {
            inSize = file.read(in.data(), std::min<qint64>(in.size(), packLeft));
            if (inSize <= 0) {
                err = QStringLiteral("the archive ends too soon");
                break;
            }
            packLeft -= inSize;
            inPos = 0;
        }
        SizeT outLen = SizeT(std::min<qint64>(out.size(), outLeft)), inLen = SizeT(inSize - inPos);
        ELzmaStatus status;
        const Byte* src = reinterpret_cast<const Byte*>(in.constData()) + inPos;
        Byte* dst = reinterpret_cast<Byte*>(out.data());
        r = two ? Lzma2Dec_DecodeToBuf(&lzma2, dst, &outLen, src, &inLen, LZMA_FINISH_ANY, &status)
                : LzmaDec_DecodeToBuf(&lzma, dst, &outLen, src, &inLen, LZMA_FINISH_ANY, &status);
        if (r != SZ_OK) {
            err = sresText(r);
            break;
        }
        inPos += qint64(inLen);
        outLeft -= qint64(outLen);
        if (outLen > 0 && !sink(out.constData(), qint64(outLen))) break;
        if (outLen == 0 && inLen == 0 && (inPos < inSize || packLeft == 0)) {
            err = QStringLiteral("damaged data");
            break;
        }
    }
    if (two) Lzma2Dec_Free(&lzma2, &kAlloc);
    else LzmaDec_Free(&lzma, &kAlloc);
    return err;
}

static std::atomic<qint64> g_keptBlock{64LL << 20};
qint64 SevenZip::keptBlock() { return g_keptBlock; }
void SevenZip::setKeptBlock(qint64 bytes) { g_keptBlock = bytes; }

SevenZip::SevenZip() : d_(std::make_unique<Impl>()) {}
SevenZip::~SevenZip() = default;

bool SevenZip::isOpen() const { return d_->open; }

void SevenZip::close() {
    d_->close();
    entries_.clear();
    file_.clear();
}

bool SevenZip::open(const QString& file) {
    close();
    static std::once_flag crc;
    std::call_once(crc, [] { CrcGenerateTable(); });
    error_.clear();
    file_ = file;
    Impl& d = *d_;
    d.file.setFileName(file);
    if (!d.file.open(QFile::ReadOnly)) {
        error_ = QStringLiteral("can't open ") + file;
        return false;
    }
    d.stream.vt.Read = qtRead;
    d.stream.vt.Seek = qtSeek;
    d.stream.file = &d.file;
    LookToRead2_CreateVTable(&d.look, False);
    d.look.buf = static_cast<Byte*>(ISzAlloc_Alloc(&kAlloc, kInBuf));
    d.look.bufSize = kInBuf;
    d.look.realStream = &d.stream.vt;
    LookToRead2_INIT(&d.look);
    SzArEx_Init(&d.db);
    const SRes r = SzArEx_Open(&d.db, &d.look.vt, &kAlloc, &kAllocTemp);
    if (r != SZ_OK) {
        error_ = file + ": " + sresText(r);
        SzArEx_Free(&d.db, &kAlloc);
        d.close();
        return false;
    }
    d.open = true;
    QVector<UInt16> name;
    entries_.reserve(int(d.db.NumFiles));
    for (UInt32 i = 0; i < d.db.NumFiles; ++i) {
        Entry e;
        const size_t len = SzArEx_GetFileNameUtf16(&d.db, i, nullptr);
        name.resize(int(len));
        SzArEx_GetFileNameUtf16(&d.db, i, name.data());
        e.name = QString::fromUtf16(reinterpret_cast<const char16_t*>(name.constData()), len > 0 ? qsizetype(len - 1) : 0);
        e.name.replace('\\', '/');
        e.dir = SzArEx_IsDir(&d.db, i);
        e.size = qint64(SzArEx_GetFileSize(&d.db, i));
        if (SzBitWithVals_Check(&d.db.MTime, i)) {
            const CNtfsFileTime& t = d.db.MTime.Vals[i];
            const qint64 ticks = (qint64(t.High) << 32) | t.Low; // 100 ns since 1601
            e.modified = QDateTime::fromMSecsSinceEpoch(ticks / 10000 - Q_INT64_C(11644473600000), Qt::UTC);
        }
        entries_ << e;
    }
    return true;
}

int SevenZip::indexOf(const QString& name) const {
    for (int i = 0; i < entries_.size(); ++i)
        if (entries_[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

qint64 SevenZip::largestSolidBlock() const {
    if (!d_->open) return 0;
    qint64 most = 0;
    for (UInt32 f = 0; f < d_->db.db.NumFolders; ++f)
        if (d_->db.FolderToFile[f + 1] - d_->db.FolderToFile[f] > 1) most = std::max(most, d_->blockSize(f));
    return most;
}

QByteArray SevenZip::readHead(int index, int bytes, const std::function<bool()>& keepGoing) {
    Impl& d = *d_;
    if (!d.open || index < 0 || index >= entries_.size()) return {};
    const UInt32 folder = d.db.FileToFolder[index];
    if (folder == kNone || d.blockSize(folder) <= keptBlock()) return read(index, keepGoing).left(bytes);
    const qint64 start = qint64(d.db.UnpackPositions[index] - d.db.UnpackPositions[d.db.FolderToFile[folder]]);
    const qint64 want = std::min<qint64>(bytes, entries_[index].size);
    QByteArray out;
    qint64 at = 0;
    bool stopped = keepGoing && !keepGoing(); // (asked first, then every megabyte decoded)
    const QString err = stopped ? QString() : d.decode(folder, [&](const char* p, qint64 n) {
        const qint64 from = std::max<qint64>(start, at), to = std::min<qint64>(start + want, at + n);
        if (to > from) out.append(p + (from - at), int(to - from));
        at += n;
        if (out.size() < want && keepGoing && !keepGoing()) stopped = true;
        return out.size() < want && !stopped;
    });
    if (!err.isEmpty()) error_ = err;
    if (stopped) {
        error_ = entries_[index].name + ": stopped";
        return {};
    }
    return out;
}

QByteArray SevenZip::read(int index, const std::function<bool()>& keepGoing) {
    Impl& d = *d_;
    if (!d.open || index < 0 || index >= entries_.size()) return {};
    if (entries_[index].dir) return {};
    const UInt32 folder = d.db.FileToFolder[index];
    if (folder == kNone) return QByteArray(""); // an empty file
    if (d.blockSize(folder) > keptBlock()) return readHead(index, int(std::min<qint64>(entries_[index].size, INT_MAX)), keepGoing);
    size_t offset = 0, processed = 0;
    const SRes r = SzArEx_Extract(&d.db, &d.look.vt, UInt32(index), &d.blockIndex, &d.outBuffer, &d.outBufferSize, &offset,
                                  &processed, &kAlloc, &kAllocTemp);
    if (r != SZ_OK) {
        error_ = entries_[index].name + ": " + sresText(r);
        d.blockIndex = kNone;
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(d.outBuffer) + offset, qsizetype(processed));
}

bool SevenZip::forEach(const std::function<bool(int, const QByteArray&)>& f) {
    Impl& d = *d_;
    if (!d.open) return false;
    QSet<UInt32> streamed;
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].dir) continue;
        const UInt32 folder = d.db.FileToFolder[i];
        if (folder == kNone) {
            if (!f(i, QByteArray(""))) return false;
            continue;
        }
        if (d.blockSize(folder) <= keptBlock()) {
            const QByteArray b = read(i);
            if (b.size() != entries_[i].size) return false;
            if (!f(i, b)) return false;
            continue;
        }
        if (streamed.contains(folder)) continue;
        streamed.insert(folder);
        // a big block: decompressed once, each of its files handed on as it's complete
        const UInt32 first = d.db.FolderToFile[folder], last = d.db.FolderToFile[folder + 1];
        UInt32 file = first;
        QByteArray cur;
        bool stopped = false;
        auto next = [&] { // the next file of the block with bytes in it
            while (file < last && (entries_[int(file)].dir || entries_[int(file)].size == 0)) {
                if (!entries_[int(file)].dir && !f(int(file), QByteArray(""))) return false;
                ++file;
            }
            return true;
        };
        if (!next()) return false;
        const QString err = d.decode(folder, [&](const char* p, qint64 n) {
            while (n > 0 && file < last) {
                const qint64 need = entries_[int(file)].size - cur.size();
                const qint64 take = std::min(need, n);
                cur.append(p, int(take));
                p += take;
                n -= take;
                if (cur.size() == entries_[int(file)].size) {
                    if (!f(int(file), cur)) {
                        stopped = true;
                        return false;
                    }
                    cur.clear();
                    ++file;
                    if (!next()) {
                        stopped = true;
                        return false;
                    }
                }
            }
            return true;
        });
        if (stopped) return false;
        if (!err.isEmpty()) {
            error_ = err;
            return false;
        }
        if (file < last) {
            error_ = QStringLiteral("the archive ends too soon");
            return false;
        }
    }
    return true;
}

namespace {
FILE* openWrite(const QString& file) {
#ifdef Q_OS_WIN
    return _wfopen(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(file).utf16()), L"wb");
#else
    return fopen(QFile::encodeName(file).constData(), "wb");
#endif
}

bool alreadyCompressed(const QString& name) {
    static const QStringList ext = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".mp4", ".mkv", ".avi", ".pdf", ".zip", ".7z"};
    for (const QString& e : ext)
        if (name.endsWith(e, Qt::CaseInsensitive)) return true;
    return false;
}
} // namespace

QString convertSevenZipToZip(const QString& sevenZip, const QString& zip, const std::function<bool(qint64, qint64)>& progress) {
    if (QFileInfo::exists(zip)) return QStringLiteral("there's already a file named ") + zip;
    SevenZip in;
    if (!in.open(sevenZip)) return in.error();
    qint64 total = 0, files = 0;
    for (const SevenZip::Entry& e : in.entries())
        if (!e.dir) {
            total += e.size;
            ++files;
        }
    const QString tmp = zip + ".new";
    QFile::remove(tmp);
    FILE* fout = openWrite(tmp);
    if (!fout) return QStringLiteral("can't write ") + tmp;
    mz_zip_archive out{};
    const bool zip64 = files > 60000 || total > Q_INT64_C(3800000000);
    if (!mz_zip_writer_init_cfile(&out, fout, zip64 ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0)) {
        fclose(fout);
        QFile::remove(tmp);
        return QStringLiteral("can't write ") + tmp;
    }
    QString error;
    qint64 done = 0;
    bool stopped = false;
    const bool ok = in.forEach([&](int i, const QByteArray& b) {
        const SevenZip::Entry& e = in.entries()[i];
        if (b.size() != e.size) {
            error = e.name + ": can't be read";
            return false;
        }
        const QByteArray name = e.name.toUtf8();
        MZ_TIME_T t = e.modified.isValid() ? e.modified.toSecsSinceEpoch() : 0;
        const mz_uint level = alreadyCompressed(e.name) ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL;
        if (!mz_zip_writer_add_mem_ex_v2(&out, name.constData(), b.constData(), size_t(b.size()), nullptr, 0, level, 0, 0,
                                         e.modified.isValid() ? &t : nullptr, nullptr, 0, nullptr, 0)) {
            error = QStringLiteral("can't write ") + e.name;
            return false;
        }
        done += b.size();
        if (progress && !progress(done, total)) {
            stopped = true;
            return false;
        }
        return true;
    });
    if (ok && !mz_zip_writer_finalize_archive(&out)) error = QStringLiteral("can't finish ") + tmp;
    mz_zip_writer_end(&out);
    fclose(fout);
    if (!ok && error.isEmpty() && !stopped) error = in.error().isEmpty() ? QStringLiteral("can't be read") : in.error();
    if (!ok || !error.isEmpty()) {
        QFile::remove(tmp);
        return stopped ? QStringLiteral("stopped") : error;
    }
    // checked: the new zip opens and has every file, then it takes its name
    {
        mz_zip_archive check{};
        const QByteArray path = QFile::encodeName(tmp);
        FILE* fc = nullptr;
#ifdef Q_OS_WIN
        fc = _wfopen(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(tmp).utf16()), L"rb");
#else
        fc = fopen(path.constData(), "rb");
#endif
        bool good = fc && mz_zip_reader_init_cfile(&check, fc, 0, 0) && qint64(mz_zip_reader_get_num_files(&check)) == files;
        if (fc) {
            mz_zip_reader_end(&check);
            fclose(fc);
        }
        if (!good) {
            QFile::remove(tmp);
            return QStringLiteral("the new zip file didn't check out");
        }
    }
    if (!QFile::rename(tmp, zip)) {
        QFile::remove(tmp);
        return QStringLiteral("can't name it ") + zip;
    }
    return {};
}

} // namespace eld
