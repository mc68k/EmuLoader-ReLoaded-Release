#include "UpdateInstall.h"

#include "ZipArchive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QSaveFile>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cstdio>
#endif

namespace eld {

namespace UpdateInstall {

namespace {

const char* kProgram = "EmuLoader-ReLoaded.exe";


// A move within the folder's drive, never a copy: a running program's file can be moved, and a
// copy would leave it where it was
bool moveFile(const QString& from, const QString& to) {
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(from).utf16()),
                       reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(to).utf16()), MOVEFILE_WRITE_THROUGH);
#else
    return std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

bool safeName(const QString& rel) {
    return !rel.isEmpty() && !rel.startsWith('/') && !rel.contains('\\') && !rel.contains(':') &&
           !rel.split('/').contains("..");
}

} // namespace

QString workFolder(const QString& programDir) { return QDir(programDir).filePath("update-download"); }
QString previousFolder(const QString& programDir) { return QDir(programDir).filePath("previous-version"); }

bool canWrite(const QString& dir) {
    const QString a = QDir(dir).filePath("update-write-check.tmp"), b = a + "2";
    QFile f(a);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("x");
    f.close();
    const bool moved = moveFile(a, b);
    QFile::remove(a);
    QFile::remove(b);
    return moved;
}

QString verify(const QString& file, qint64 size, const QString& sha256) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return QCoreApplication::translate("eld::UpdateInstall", "the download can't be read");
    if (size > 0 && f.size() != size)
        return QCoreApplication::translate("eld::UpdateInstall", "the download is incomplete (%1 of %2 bytes)").arg(f.size()).arg(size);
    if (sha256.isEmpty()) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QCoreApplication::translate("eld::UpdateInstall", "the download can't be read");
    if (QString::fromLatin1(h.result().toHex()) != sha256.toLower()) return QCoreApplication::translate("eld::UpdateInstall", "the download is damaged (its checksum doesn't match)");
    return {};
}

bool unpack(const QString& zip, const QString& into, QString* error, const std::function<void(int, int)>& progress) {
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    ZipArchive z;
    if (!z.open(zip) || z.isSevenZip()) return fail(QCoreApplication::translate("eld::UpdateInstall", "the download isn't a zip file"));
    const auto entries = z.entries();
    // the folder the program is in ("EmuLoader-ReLoaded/", or the zip's top)
    QString root;
    bool found = false;
    for (const auto& e : entries) {
        const QString name = e.name;
        if (name.compare(kProgram, Qt::CaseInsensitive) == 0 || name.endsWith(QString("/") + kProgram, Qt::CaseInsensitive)) {
            if (!found || name.size() < root.size() + int(qstrlen(kProgram))) root = name.left(name.size() - int(qstrlen(kProgram)));
            found = true;
        }
    }
    if (!found) return fail(QCoreApplication::translate("eld::UpdateInstall", "there's no %1 in the download").arg(kProgram));
    int total = 0, done = 0;
    for (const auto& e : entries)
        if (e.name.startsWith(root, Qt::CaseInsensitive) && !e.name.endsWith('/')) ++total;
    QDir(into).removeRecursively();
    if (!QDir().mkpath(into)) return fail(QCoreApplication::translate("eld::UpdateInstall", "%1 can't be created").arg(QDir::toNativeSeparators(into)));
    for (const auto& e : entries) {
        if (!e.name.startsWith(root, Qt::CaseInsensitive)) continue;
        const QString rel = e.name.mid(root.size());
        if (rel.endsWith('/')) continue;
        if (!safeName(rel)) return fail(QCoreApplication::translate("eld::UpdateInstall", "the download has a file outside its folder (%1)").arg(e.name));
        const QByteArray data = z.read(e.name);
        if (data.size() != e.size) return fail(QCoreApplication::translate("eld::UpdateInstall", "%1 can't be read from the download").arg(e.name));
        const QString to = QDir(into).filePath(rel);
        QDir().mkpath(QFileInfo(to).absolutePath());
        QSaveFile f(to);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
            return fail(QCoreApplication::translate("eld::UpdateInstall", "%1 can't be written").arg(QDir::toNativeSeparators(to)));
        if (progress) progress(++done, total);
    }
    return true;
}

bool install(const QString& unpacked, const QString& programDir, const QString& previousDir, const QString& about,
             QString* error) {
    // the files to put in place, relative to unpacked
    QStringList files;
    QDirIterator it(unpacked, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) files << QDir(unpacked).relativeFilePath(it.next());
    if (files.isEmpty()) {
        if (error) *error = QCoreApplication::translate("eld::UpdateInstall", "there are no files to install");
        return false;
    }
    if (!QDir(previousDir).removeRecursively() && QFileInfo::exists(previousDir)) {
        if (error) *error = QCoreApplication::translate("eld::UpdateInstall", "%1 can't be emptied").arg(QDir::toNativeSeparators(previousDir));
        return false;
    }
    QDir().mkpath(previousDir);

    QVector<QPair<QString, QString>> movedAside; // (where it was, where it is now)
    QVector<QPair<QString, QString>> placed;     // new files put in place (where they came from, where they are)
    auto undo = [&] {
        for (int i = placed.size() - 1; i >= 0; --i) moveFile(placed[i].second, placed[i].first);
        for (int i = movedAside.size() - 1; i >= 0; --i) moveFile(movedAside[i].second, movedAside[i].first);
    };
    for (const QString& rel : files) {
        const QString target = QDir(programDir).filePath(rel);
        if (QFileInfo::exists(target)) {
            const QString aside = QDir(previousDir).filePath(rel);
            QDir().mkpath(QFileInfo(aside).absolutePath());
            if (!moveFile(target, aside)) {
                undo();
                if (error) *error = QCoreApplication::translate("eld::UpdateInstall", "%1 can't be replaced (is it open in another program?)").arg(QDir::toNativeSeparators(target));
                return false;
            }
            movedAside.append({target, aside});
        }
        QDir().mkpath(QFileInfo(target).absolutePath());
        const QString from = QDir(unpacked).filePath(rel);
        if (!moveFile(from, target)) {
            undo();
            if (error) *error = QCoreApplication::translate("eld::UpdateInstall", "%1 can't be put in place").arg(QDir::toNativeSeparators(target));
            return false;
        }
        placed.append({from, target});
    }
    if (!about.isEmpty()) {
        QFile f(QDir(previousDir).filePath("About this folder.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(about.toUtf8());
    }
    return true;
}

void cleanUp(const QString& programDir) {
    const QString work = workFolder(programDir);
    if (QFileInfo::exists(work)) QDir(work).removeRecursively();
}

} // namespace UpdateInstall

} // namespace eld
