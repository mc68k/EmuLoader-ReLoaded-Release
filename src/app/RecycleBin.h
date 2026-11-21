// RecycleBin - files deleted by the program go to the Recycle Bin (the Trash), see Trash.h: on
// Windows, Windows asks before deleting a file it can't recycle; elsewhere the program asks once
// before deleting such files for good. Many files: a progress window (Cancel stops it).
#pragma once

#include <QStringList>

class QWidget;

namespace eld {

// Returns the files that are gone (to the Recycle Bin, or deleted when allowed).
QStringList moveToRecycleBin(const QStringList& files, QWidget* parent);

} // namespace eld
