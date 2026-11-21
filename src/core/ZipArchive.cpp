#include "ZipArchive.h"
#include "SevenZip.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <cstdio>

extern "C" {
#include "miniz.h"
}

namespace eld {

namespace {
FILE* openFile(const QString& file, bool write) {
#ifdef Q_OS_WIN
    return _wfopen(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(file).utf16()), write ? L"wb" : L"rb");
#else
    return fopen(QFile::encodeName(file).constData(), write ? "wb" : "rb");
#endif
}
} // namespace

struct ZipArchive::Impl {
    mz_zip_archive za{};
    FILE* fp = nullptr;
    bool open = false;
    std::unique_ptr<SevenZip> sz; // a .7z
};

bool ZipArchive::sevenZipName(const QString& file) { return file.endsWith(QLatin1String(".7z"), Qt::CaseInsensitive); }
bool ZipArchive::isSevenZip() const { return d_->sz != nullptr; }
bool ZipArchive::slow() const {
    QMutexLocker lock(&mutex_);
    return d_->sz && d_->sz->slow();
}

ZipArchive::ZipArchive() : d_(new Impl) {}
ZipArchive::~ZipArchive() { close(); }

bool ZipArchive::isOpen() const { return d_->open; }

bool ZipArchive::open(const QString& file) {
    close();
    QMutexLocker lock(&mutex_);
    if (sevenZipName(file)) {
        auto sz = std::make_unique<SevenZip>();
        if (!sz->open(file)) return false;
        const auto& es = sz->entries();
        index_.reserve(es.size());
        for (int i = 0; i < es.size(); ++i)
            if (!es[i].dir) index_.insert(es[i].name.toLower(), unsigned(i));
        d_->sz = std::move(sz);
        d_->open = true;
        file_ = file;
        return true;
    }
    d_->fp = openFile(file, false);
    if (!d_->fp) return false;
    d_->za = mz_zip_archive{};
    if (!mz_zip_reader_init_cfile(&d_->za, d_->fp, 0, 0)) {
        fclose(d_->fp);
        d_->fp = nullptr;
        return false;
    }
    d_->open = true;
    file_ = file;
    const mz_uint n = mz_zip_reader_get_num_files(&d_->za);
    index_.reserve(n);
    for (mz_uint i = 0; i < n; ++i) {
        char buf[1024];
        mz_zip_reader_get_filename(&d_->za, i, buf, sizeof buf);
        index_.insert(QString::fromUtf8(buf).replace('\\', '/').toLower(), i);
    }
    return true;
}

void ZipArchive::close() {
    QMutexLocker lock(&mutex_);
    if (d_->open && !d_->sz) mz_zip_reader_end(&d_->za);
    d_->sz.reset();
    if (d_->fp) fclose(d_->fp);
    d_->fp = nullptr;
    d_->open = false;
    index_.clear();
    file_.clear();
}

QByteArray ZipArchive::read(const QString& name, const std::function<bool()>& keepGoing) const {
    QMutexLocker lock(&mutex_);
    if (keepGoing && !keepGoing()) return {}; // (not wanted any more, while waiting its turn)
    auto it = index_.find(name.toLower());
    if (!d_->open || it == index_.end()) return {};
    if (d_->sz) return d_->sz->read(int(*it), keepGoing);
    size_t size = 0;
    void* p = mz_zip_reader_extract_to_heap(&d_->za, *it, &size, 0);
    if (!p) return {};
    QByteArray out(static_cast<const char*>(p), int(size));
    mz_free(p);
    return out;
}

QVector<ZipArchive::Entry> ZipArchive::entries() const {
    QMutexLocker lock(&mutex_);
    QVector<Entry> out;
    if (!d_->open) return out;
    if (d_->sz) {
        for (const SevenZip::Entry& s : d_->sz->entries())
            if (!s.dir) out << Entry{s.name, s.size, s.modified.toLocalTime()};
        return out;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&d_->za);
    out.reserve(int(n));
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&d_->za, i, &st) || st.m_is_directory) continue;
        Entry e;
        e.name = QString::fromUtf8(st.m_filename).replace('\\', '/');
        e.size = qint64(st.m_uncomp_size);
        e.modified = QDateTime::fromSecsSinceEpoch(qint64(st.m_time));
        out << e;
    }
    return out;
}

QByteArray ZipArchive::readHead(const QString& name, int bytes) const {
    QMutexLocker lock(&mutex_);
    auto it = index_.find(name.toLower());
    if (!d_->open || it == index_.end() || bytes <= 0) return {};
    if (d_->sz) return d_->sz->readHead(int(*it), bytes);
    mz_zip_reader_extract_iter_state* st = mz_zip_reader_extract_iter_new(&d_->za, *it, 0);
    if (!st) return {};
    QByteArray out(bytes, '\0');
    size_t got = mz_zip_reader_extract_iter_read(st, out.data(), size_t(bytes));
    mz_zip_reader_extract_iter_free(st);
    out.truncate(int(got));
    return out;
}

// ---------------------------------------------------------------- the zips kept open

namespace {
struct OpenZip {
    std::shared_ptr<ZipArchive> zip;
    qint64 size = 0;
    QDateTime modified;
};
QMutex& sharedMutex() {
    static QMutex m;
    return m;
}
QHash<QString, OpenZip>& sharedZips() {
    static QHash<QString, OpenZip> h;
    return h;
}
} // namespace

std::shared_ptr<ZipArchive> ZipArchive::shared(const QString& file) {
    const QFileInfo fi(file);
    const QString key = fi.absoluteFilePath();
    QMutexLocker lock(&sharedMutex());
    auto& zips = sharedZips();
    auto it = zips.find(key);
    if (it != zips.end() && it->size == fi.size() && it->modified == fi.lastModified()) return it->zip;
    auto z = std::make_shared<ZipArchive>();
    if (!z->open(key)) {
        zips.remove(key);
        return {};
    }
    zips.insert(key, {z, fi.size(), fi.lastModified()});
    return z;
}

void ZipArchive::release(const QString& file) {
    QMutexLocker lock(&sharedMutex());
    if (file.isEmpty()) sharedZips().clear();
    else sharedZips().remove(QFileInfo(file).absoluteFilePath());
}

// ---------------------------------------------------------------- Zip Images / Unzip Images

namespace {
// The new zip in place of the old one, once it's checked (the old one kept until then)
QString replaceZip(const QString& zip, const QString& tmp, int expected) {
    ZipArchive::release(zip); // not held open by the image panels
    ZipArchive check;
    if (!check.open(tmp) || check.entries().size() < expected) {
        check.close();
        QFile::remove(tmp);
        return QStringLiteral("the new zip file is damaged");
    }
    check.close();
    if (!QFileInfo::exists(zip)) {
        if (QFile::rename(tmp, zip)) return {};
        QFile::remove(tmp);
        return QStringLiteral("can't write ") + zip;
    }
    const QString bak = zip + ".old";
    QFile::remove(bak);
    if (!QFile::rename(zip, bak)) {
        QFile::remove(tmp);
        return QStringLiteral("the zip file is in use");
    }
    if (!QFile::rename(tmp, zip)) {
        QFile::rename(bak, zip);
        QFile::remove(tmp);
        return QStringLiteral("can't replace the zip file");
    }
    QFile::remove(bak);
    return {};
}
} // namespace

QString addToZip(const QString& zip, const QVector<ZipAdd>& files, bool replace, const std::function<bool(int, int)>& progress,
                 QStringList* added) {
    QSet<QString> adding;
    for (const ZipAdd& a : files) adding.insert(a.name.toLower());
    mz_zip_archive in{};
    FILE* fin = nullptr;
    const bool haveOld = QFileInfo::exists(zip);
    if (haveOld) {
        fin = openFile(zip, false);
        if (!fin || !mz_zip_reader_init_cfile(&in, fin, 0, 0)) {
            if (fin) fclose(fin);
            return QStringLiteral("can't read ") + zip;
        }
    }
    const mz_uint n = haveOld ? mz_zip_reader_get_num_files(&in) : 0;
    qint64 bytes = haveOld ? QFileInfo(zip).size() : 0;
    for (const ZipAdd& a : files) bytes += QFileInfo(a.file).size();
    const bool zip64 = int(n) + files.size() > 60000 || bytes > Q_INT64_C(3800000000);
    const QString tmp = zip + ".new";
    QFile::remove(tmp);
    FILE* fout = openFile(tmp, true);
    mz_zip_archive out{};
    QString error;
    int expected = 0;
    QSet<QString> inZip;
    const int total = int(n) + files.size();
    auto run = [&]() -> bool {
        if (!fout || !mz_zip_writer_init_cfile(&out, fout, zip64 ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0)) {
            error = QStringLiteral("can't write ") + tmp;
            return false;
        }
        for (mz_uint i = 0; i < n; ++i) { // the zip's own files
            if (progress && i % 200 == 0 && !progress(int(i), total)) { error = QStringLiteral("canceled"); return false; }
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&in, i, &st)) { error = QStringLiteral("damaged zip file"); return false; }
            const QString key = QString::fromUtf8(st.m_filename).replace('\\', '/').toLower();
            if (replace && adding.contains(key)) continue;
            if (!mz_zip_writer_add_from_zip_reader(&out, &in, i)) { error = QStringLiteral("can't copy ") + key; return false; }
            inZip.insert(key);
            if (!st.m_is_directory) ++expected;
        }
        for (int i = 0; i < files.size(); ++i) {
            if (progress && i % 20 == 0 && !progress(int(n) + i, total)) { error = QStringLiteral("canceled"); return false; }
            const ZipAdd& a = files[i];
            const QString key = a.name.toLower();
            if (inZip.contains(key)) continue; // the zip's own stays (or added twice)
            QFile f(a.file);
            if (!f.open(QFile::ReadOnly)) { error = QStringLiteral("can't read ") + a.file; return false; }
            const QByteArray data = f.readAll();
            MZ_TIME_T t = QFileInfo(a.file).lastModified().toSecsSinceEpoch();
            const QByteArray name = a.name.toUtf8();
            if (!mz_zip_writer_add_mem_ex_v2(&out, name.constData(), data.constData(), size_t(data.size()), nullptr, 0, 0, 0, 0, &t,
                                             nullptr, 0, nullptr, 0)) {
                error = QStringLiteral("can't add ") + a.name;
                return false;
            }
            inZip.insert(key);
            ++expected;
            if (added) *added << a.file;
        }
        if (!mz_zip_writer_finalize_archive(&out)) { error = QStringLiteral("can't finish ") + tmp; return false; }
        return true;
    };
    bool ok = run();
    mz_zip_writer_end(&out);
    if (fout) fclose(fout);
    if (haveOld) {
        mz_zip_reader_end(&in);
        fclose(fin);
    }
    if (!ok) {
        QFile::remove(tmp);
        if (added) added->clear();
        return error;
    }
    const QString r = replaceZip(zip, tmp, expected);
    if (!r.isEmpty() && added) added->clear();
    return r;
}

QString extractZip(const QString& zip, const QString& folder, bool replace, const std::function<bool(int, int)>& progress,
                   QStringList* written) {
    if (ZipArchive::sevenZipName(zip)) { // a .7z: each block decompressed once
        SevenZip in;
        if (!in.open(zip)) return QStringLiteral("can't read ") + zip;
        const QDir root(folder);
        const int n = in.entries().size();
        QString error;
        int done = 0;
        const bool ok = in.forEach([&](int i, const QByteArray& b) {
            if (progress && done++ % 20 == 0 && !progress(done, n)) {
                error = QStringLiteral("canceled");
                return false;
            }
            const SevenZip::Entry& e = in.entries()[i];
            const QString to = QDir::cleanPath(root.filePath(e.name));
            if (!to.startsWith(QDir::cleanPath(root.absolutePath()) + "/")) return true; // no "../" out of the folder
            if (QFileInfo::exists(to) && !replace) return true;
            QDir().mkpath(QFileInfo(to).absolutePath());
            QFile f(to);
            if (!f.open(QFile::WriteOnly) || f.write(b) != b.size()) {
                error = QStringLiteral("can't write ") + to;
                return false;
            }
            f.close();
            if (e.modified.isValid()) f.setFileTime(e.modified, QFileDevice::FileModificationTime);
            if (written) *written << to;
            return true;
        });
        if (!ok && error.isEmpty()) error = in.error().isEmpty() ? QStringLiteral("can't read ") + zip : in.error();
        return error;
    }
    mz_zip_archive in{};
    FILE* fin = openFile(zip, false);
    if (!fin || !mz_zip_reader_init_cfile(&in, fin, 0, 0)) {
        if (fin) fclose(fin);
        return QStringLiteral("can't read ") + zip;
    }
    QString error;
    const mz_uint n = mz_zip_reader_get_num_files(&in);
    const QDir root(folder);
    for (mz_uint i = 0; i < n; ++i) {
        if (progress && i % 20 == 0 && !progress(int(i), int(n))) { error = QStringLiteral("canceled"); break; }
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&in, i, &st) || st.m_is_directory) continue;
        const QString name = QString::fromUtf8(st.m_filename).replace('\\', '/');
        const QString to = QDir::cleanPath(root.filePath(name));
        if (!to.startsWith(QDir::cleanPath(root.absolutePath()) + "/")) continue; // no "../" out of the folder
        if (QFileInfo::exists(to) && !replace) continue;
        QDir().mkpath(QFileInfo(to).absolutePath());
        FILE* f = openFile(to, true);
        if (!f) { error = QStringLiteral("can't write ") + to; break; }
        const bool ok = mz_zip_reader_extract_to_cfile(&in, i, f, 0);
        fclose(f);
        if (!ok) { error = QStringLiteral("can't extract ") + name; QFile::remove(to); break; }
        QFile(to).setFileTime(QDateTime::fromSecsSinceEpoch(qint64(st.m_time)), QFileDevice::FileModificationTime);
        if (written) *written << to;
    }
    mz_zip_reader_end(&in);
    fclose(fin);
    return error;
}

// ---------------------------------------------------------------- ZipEditor

void ZipEditor::remove(const QString& entry) {
    renamed_.remove(entry.toLower());
    removed_.insert(entry.toLower(), entry);
}

void ZipEditor::rename(const QString& entry, const QString& to) {
    if (to == entry) renamed_.remove(entry.toLower());
    else renamed_.insert(entry.toLower(), {entry, to});
}

void ZipEditor::undo(const QString& entry) {
    removed_.remove(entry.toLower());
    renamed_.remove(entry.toLower());
}

QString ZipEditor::renamedTo(const QString& entry) const {
    auto it = renamed_.find(entry.toLower());
    return it == renamed_.end() ? QString() : it->second;
}

QString ZipEditor::apply(const QString& removedDir, QStringList* extracted, const std::function<bool(int, int)>& progress) {
    if (!changes()) return {};
    mz_zip_archive in{};
    FILE* fin = openFile(zip_, false);
    if (!fin) return QStringLiteral("can't open the zip file");
    if (!mz_zip_reader_init_cfile(&in, fin, 0, 0)) {
        fclose(fin);
        return QStringLiteral("not a zip file");
    }
    const QString tmp = zip_ + ".new";
    QFile::remove(tmp);
    FILE* fout = nullptr;
    mz_zip_archive out{};
    bool writing = false;
    QString error;
    int kept = 0;
    const mz_uint n = mz_zip_reader_get_num_files(&in);
    auto fail = [&](const QString& e) {
        error = e;
        return false;
    };
    auto run = [&]() -> bool {
        // the removed files first, so they're safe before anything changes
        if (!removed_.isEmpty()) {
            if (!QDir().mkpath(removedDir)) return fail(QStringLiteral("can't create ") + removedDir);
            for (mz_uint i = 0; i < n; ++i) {
                mz_zip_archive_file_stat st;
                if (!mz_zip_reader_file_stat(&in, i, &st) || st.m_is_directory) continue;
                QString name = QString::fromUtf8(st.m_filename).replace('\\', '/');
                if (!removed_.contains(name.toLower())) continue;
                QString to = QDir(removedDir).filePath(QString(name).replace('/', ' '));
                FILE* f = openFile(to, true);
                if (!f) return fail(QStringLiteral("can't write ") + to);
                bool ok = mz_zip_reader_extract_to_cfile(&in, i, f, 0);
                fclose(f);
                if (!ok) return fail(QStringLiteral("can't extract ") + name);
                if (extracted) *extracted << to;
            }
        }
        fout = openFile(tmp, true);
        if (!fout) return fail(QStringLiteral("can't write ") + tmp);
        // a big zip stays a ZIP64 one (files copied as they are can't make the switch on the way)
        const bool zip64 = n > 60000 || QFileInfo(zip_).size() > Q_INT64_C(3800000000);
        if (!mz_zip_writer_init_cfile(&out, fout, zip64 ? MZ_ZIP_FLAG_WRITE_ZIP64 : 0)) return fail(QStringLiteral("can't write ") + tmp);
        writing = true;
        for (mz_uint i = 0; i < n; ++i) {
            if (progress && i % 200 == 0 && !progress(int(i), int(n))) return fail(QStringLiteral("canceled"));
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&in, i, &st)) return fail(QStringLiteral("damaged zip file"));
            QString name = QString::fromUtf8(st.m_filename).replace('\\', '/');
            const QString key = name.toLower();
            if (removed_.contains(key)) continue;
            auto rn = renamed_.find(key);
            if (rn == renamed_.end()) {
                if (!mz_zip_writer_add_from_zip_reader(&out, &in, i)) return fail(QStringLiteral("can't copy ") + name);
            } else {
                size_t size = 0;
                void* p = mz_zip_reader_extract_to_heap(&in, i, &size, 0);
                if (!p) return fail(QStringLiteral("can't extract ") + name);
                const QByteArray to = rn->second.toUtf8();
                MZ_TIME_T t = st.m_time;
                bool ok = mz_zip_writer_add_mem_ex_v2(&out, to.constData(), p, size, nullptr, 0,
                                                      st.m_method == 0 ? 0 : MZ_DEFAULT_LEVEL, 0, 0, &t, nullptr, 0, nullptr, 0);
                mz_free(p);
                if (!ok) return fail(QStringLiteral("can't add ") + rn->second);
            }
            if (!st.m_is_directory) ++kept;
        }
        if (!mz_zip_writer_finalize_archive(&out)) return fail(QStringLiteral("can't finish ") + tmp);
        return true;
    };
    bool ok = run();
    if (writing) mz_zip_writer_end(&out);
    if (fout) fclose(fout);
    mz_zip_reader_end(&in);
    fclose(fin);
    if (ok) { // the new zip is checked before it takes the old one's place
        ZipArchive check;
        if (!check.open(tmp) || check.entries().size() < kept) ok = fail(QStringLiteral("the new zip file is damaged"));
        check.close();
    }
    if (!ok) {
        QFile::remove(tmp);
        return error;
    }
    ZipArchive::release(zip_); // not held open by the image panels
    const QString bak = zip_ + ".old";
    QFile::remove(bak);
    if (!QFile::rename(zip_, bak)) {
        QFile::remove(tmp);
        return QStringLiteral("the zip file is in use");
    }
    if (!QFile::rename(tmp, zip_)) {
        QFile::rename(bak, zip_);
        QFile::remove(tmp);
        return QStringLiteral("can't replace the zip file");
    }
    QFile::remove(bak);
    if (!kept) QFile::remove(zip_); // nothing left in it
    removed_.clear();
    renamed_.clear();
    return {};
}

} // namespace eld
