// Trash - files to the Recycle Bin, never deleted for good without asking.
//
// Qt's QFile::moveToTrash asks Windows for no confirmation at all, and Windows then deletes a file
// it can't recycle (a USB or network drive, a full or switched off Recycle Bin) for good without a
// word. So on Windows its own delete with Undo is used here, with the warning before a permanent
// delete ("Are you sure you want to permanently delete...?"): a file kept that way stays where it
// is. Elsewhere (and under Wine) QFile::moveToTrash, which leaves a file it can't move alone.
#pragma once

#include <QStringList>
#include <functional>

namespace eld::trash {

// The files gone to the Recycle Bin (or deleted after Windows asked). owner: the window Windows'
// questions belong to (a HWND), 0 for none. progress(done, total) returns false to stop.
QStringList recycle(const QStringList& files, quintptr owner = 0, const std::function<bool(int, int)>& progress = {});

// Windows itself asks before deleting a file it can't recycle (so the program doesn't)
bool systemAsks();

} // namespace eld::trash
