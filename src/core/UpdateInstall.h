// UpdateInstall - Install Update's steps once the new version's zip is downloaded: the zip
// checked (its size and SHA-256, as GitHub gives them), unpacked next to the program, and its
// files put in the program folder. Each file it replaces is moved to the previous-version folder
// first (Windows lets a running program's files be moved, not deleted), so the version that was
// running is kept there, one version only. Nothing else in the folder is touched: settings,
// games lists and the rest aren't in the zip. If a file can't be put in place, every file
// moved so far is put back.
#pragma once

#include <QString>
#include <functional>

namespace eld {

namespace UpdateInstall {

// The folders in the program folder: the download and its unpacked files (removed after), and
// the version replaced
QString workFolder(const QString& programDir);     // <program>/update-download
QString previousFolder(const QString& programDir); // <program>/previous-version

// Can files be created and moved in the folder? (not in Program Files without rights, ...)
bool canWrite(const QString& dir);

// "" when the file has the size and SHA-256 given (an empty sha256: not checked), else why not
QString verify(const QString& file, qint64 size, const QString& sha256);

// Unpacks the program's files: the zip's folder that has EmuLoader-ReLoaded.exe, into `into`
// (emptied first). False, with why, when there's no program in it or a file can't be written.
// progress(done, total): after each file (called on the thread unpacking).
bool unpack(const QString& zip, const QString& into, QString* error, const std::function<void(int, int)>& progress = {});

// Puts the files of `unpacked` in programDir, the ones they replace moved to previousDir
// (emptied first; `about` is written there as "About this folder.txt"). False, with why, after
// putting everything back as it was.
bool install(const QString& unpacked, const QString& programDir, const QString& previousDir, const QString& about,
             QString* error);

// The download folder left by an update (removed at the next start)
void cleanUp(const QString& programDir);

} // namespace UpdateInstall

} // namespace eld
