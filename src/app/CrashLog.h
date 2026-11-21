// CrashLog - when the program crashes on Windows, a few lines in EmuLoaderRL\crash.log: the
// version and build, the error, and where in the program it stopped (each frame as a module and
// an offset, e.g. EmuLoader-ReLoaded.exe+0x12a4f0). tools/crash-lookup.sh turns those into
// source files and lines with the build's debug copy (dist/symbols/<build>).
#pragma once

#include <QString>

namespace eld {

void installCrashLog(const QString& file); // again with another file: that one from now on

} // namespace eld
