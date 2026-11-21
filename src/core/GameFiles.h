// GameFiles - a game's files on disk, for Delete / Copy / Move Selected Games Files (the
// original's TFormDeleteGameFiles.AddFiles / AddConfigFiles and TFormCopyMoveGameFiles):
//   ROM     the set's .zip or .7z, in the first ROM folder that has it; for a merged clone
//           without a file of its own, the parent's file (marked, since it holds other games)
//   CHD     the .chd files in <ROM folder>\<game> folder
//   Config  <cfg>\<game>.cfg, <nvram>\<game>.nv, the files in <nvram>\<game>\ (and
//           <nvram>\<game>_1 .. _65 for games with a BIOS)
#pragma once

#include "ElDatabase.h"

#include <QString>
#include <QStringList>
#include <functional>

namespace eld {

struct GameFile {
    enum Kind { Rom, Chd, Config };
    QString game;     // the set it was found for
    QString path;
    Kind kind = Rom;
    bool parentSet = false; // a merged clone's parent file
    qint64 size = 0;
};

QList<GameFile> findGameFiles(const MameGame& g, const QStringList& romFolders, const QString& cfgDir,
                              const QString& nvramDir);

// Delete (to the Recycle Bin), copy or move files. Copy and move go to dest, or to
// dest\<systemFolder> when it's given; a CHD goes into a folder named after its game, where
// MAME looks for it. Returns the problems, one line each; progress(done, file) after each file,
// false from it stops.
enum class FileAction { Delete, Copy, Move };
QStringList processGameFiles(const QList<GameFile>& files, FileAction action, const QString& dest,
                             const QString& systemFolder, bool overwrite,
                             const std::function<bool(int, const QString&)>& progress = {});
QString destinationFor(const GameFile& f, const QString& dest, const QString& systemFolder);

} // namespace eld
