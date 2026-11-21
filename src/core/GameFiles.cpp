#include "GameFiles.h"
#include "Trash.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace eld {

static GameFile make(const QString& game, const QString& path, GameFile::Kind kind, bool parent = false) {
    GameFile f;
    f.game = game;
    f.path = QDir::cleanPath(path);
    f.kind = kind;
    f.parentSet = parent;
    f.size = QFileInfo(path).size();
    return f;
}

static QString findSet(const QString& name, const QStringList& romFolders) {
    for (const QString& dir : romFolders)
        for (const char* ext : {".zip", ".7z"}) {
            QString p = QDir(dir).filePath(name + ext);
            if (QFileInfo::exists(p)) return p;
        }
    return {};
}

QList<GameFile> findGameFiles(const MameGame& g, const QStringList& romFolders, const QString& cfgDir,
                              const QString& nvramDir) {
    QList<GameFile> out;
    // ROM set (SearchZIPFolder); a merged clone lives in its parent's file.
    QString set = findSet(g.name, romFolders);
    if (!set.isEmpty()) out << make(g.name, set, GameFile::Rom);
    else if (!g.cloneOf.isEmpty() && g.merged) {
        QString parent = findSet(g.cloneOf, romFolders);
        if (!parent.isEmpty()) out << make(g.name, parent, GameFile::Rom, true);
    }
    // CHDs in <ROM folder>\<game>\ (SearchCHDSimpleScan)
    for (const QString& dir : romFolders) {
        QDir d(QDir(dir).filePath(g.name));
        if (!d.exists()) continue;
        for (const QFileInfo& fi : d.entryInfoList({"*.chd"}, QDir::Files, QDir::Name))
            out << make(g.name, fi.absoluteFilePath(), GameFile::Chd);
    }
    // Config files (AddConfigFiles): .cfg, the old .nv and MAME's nvram\<game>\ folder
    if (!cfgDir.isEmpty()) {
        QString cfg = QDir(cfgDir).filePath(g.name + ".cfg");
        if (QFileInfo::exists(cfg)) out << make(g.name, cfg, GameFile::Config);
    }
    if (!nvramDir.isEmpty()) {
        QString nv = QDir(nvramDir).filePath(g.name + ".nv");
        if (QFileInfo::exists(nv)) out << make(g.name, nv, GameFile::Config);
        QStringList folders{g.name};
        if (!g.bios.isEmpty())
            for (int i = 1; i <= 65; ++i) folders << g.name + "_" + QString::number(i);
        for (const QString& f : folders) {
            QDir d(QDir(nvramDir).filePath(f));
            if (!d.exists()) continue;
            for (const QFileInfo& fi : d.entryInfoList(QDir::Files, QDir::Name))
                out << make(g.name, fi.absoluteFilePath(), GameFile::Config);
        }
    }
    return out;
}

QString destinationFor(const GameFile& f, const QString& dest, const QString& systemFolder) {
    QDir d(dest);
    QString base = systemFolder.isEmpty() ? d.path() : d.filePath(systemFolder);
    QString name = QFileInfo(f.path).fileName();
    if (f.kind == GameFile::Chd) return QDir(base).filePath(QFileInfo(f.path).dir().dirName() + "/" + name);
    return QDir(base).filePath(name);
}

QStringList processGameFiles(const QList<GameFile>& files, FileAction action, const QString& dest,
                             const QString& systemFolder, bool overwrite,
                             const std::function<bool(int, const QString&)>& progress) {
    QStringList problems;
    QStringList done; // a parent set shared by several clones is handled once
    int n = 0;
    for (const GameFile& f : files) {
        if (progress && !progress(n, f.path)) {
            problems << QCoreApplication::translate("eld::GameFiles", "Stopped before %1").arg(QDir::toNativeSeparators(f.path));
            break;
        }
        ++n;
        if (done.contains(f.path)) continue;
        done << f.path;
        if (!QFileInfo::exists(f.path)) {
            problems << QCoreApplication::translate("eld::GameFiles", "Not found: %1").arg(QDir::toNativeSeparators(f.path));
            continue;
        }
        if (action == FileAction::Delete) {
            // The original deletes for good; the Recycle Bin lets a mistake be undone.
            if (trash::recycle({f.path}).isEmpty())
                problems << QCoreApplication::translate("eld::GameFiles", "Could not move to the Recycle Bin: %1")
                                .arg(QDir::toNativeSeparators(f.path));
            continue;
        }
        QString to = destinationFor(f, dest, systemFolder);
        if (QFileInfo(to).absoluteFilePath() == QFileInfo(f.path).absoluteFilePath()) continue; // already there
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (QFileInfo::exists(to)) {
            if (!overwrite) {
                problems << QCoreApplication::translate("eld::GameFiles", "Already there (not overwritten): %1")
                                .arg(QDir::toNativeSeparators(to));
                continue;
            }
            QFile::remove(to);
        }
        bool ok = false;
        if (action == FileAction::Move) {
            ok = QFile::rename(f.path, to); // same drive: instant
            if (!ok && QFile::copy(f.path, to)) ok = QFile::remove(f.path) || (QFile::remove(to), false);
        } else {
            ok = QFile::copy(f.path, to);
        }
        if (!ok)
            problems << QCoreApplication::translate("eld::GameFiles", "Could not %1 %2 to %3")
                            .arg(action == FileAction::Move ? QCoreApplication::translate("eld::GameFiles", "move")
                                                            : QCoreApplication::translate("eld::GameFiles", "copy"),
                                 QDir::toNativeSeparators(f.path), QDir::toNativeSeparators(to));
    }
    if (progress) progress(n, {});
    return problems;
}

} // namespace eld
