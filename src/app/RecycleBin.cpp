#include "RecycleBin.h"

#include "ProgressWindow.h"
#include "Trash.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <memory>

namespace eld {

QStringList moveToRecycleBin(const QStringList& files, QWidget* parent) {
    // Many files take a while: shown as it goes, Cancel stops
    std::unique_ptr<ProgressWindow> win;
    if (files.size() > 30) {
        win = std::make_unique<ProgressWindow>(QApplication::translate("RecycleBin", "Recycle Bin"), parent);
        win->setCancelable(true);
        win->step(QApplication::translate("RecycleBin", "Moving files to the Recycle Bin"), int(files.size()));
    }
    const QLocale loc;
    QStringList gone = trash::recycle(files, parent ? quintptr(parent->window()->winId()) : 0, [&](int done, int total) {
        if (!win) return true;
        win->setProgress(done, QApplication::translate("RecycleBin", "%1 of %2").arg(loc.toString(done), loc.toString(total)));
        return !win->canceled();
    });
    const bool canceled = win && win->canceled();
    if (win) win->close();
    if (trash::systemAsks() || canceled) return gone; // Windows asked about what it couldn't recycle
    QStringList stuck;
    for (const QString& f : files)
        if (QFile::exists(f) && !gone.contains(f)) stuck << f;
    if (stuck.isEmpty()) return gone;
    QString list;
    for (int i = 0; i < stuck.size() && i < 10; ++i) list += QDir::toNativeSeparators(stuck[i]) + "\n";
    if (stuck.size() > 10) list += QApplication::translate("RecycleBin", "... and %1 more").arg(stuck.size() - 10) + "\n";
    if (QMessageBox::question(parent, QApplication::translate("RecycleBin", "Recycle Bin"),
                              QApplication::translate("RecycleBin", "These files can't go to the Recycle Bin (it may not be available "
                                                                    "for their drive):\n\n%1\nDelete them permanently?")
                                  .arg(list),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return gone;
    for (const QString& f : stuck)
        if (QFile::remove(f)) gone << f;
    return gone;
}

} // namespace eld
