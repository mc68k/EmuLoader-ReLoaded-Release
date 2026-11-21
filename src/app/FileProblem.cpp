#include "FileProblem.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QStorageInfo>

namespace eld {

QString FileProblem::whyNotWritable(const QString& path) {
    const QFileInfo f(path);
    // the nearest folder that's there
    QString dir = f.absolutePath();
    while (!dir.isEmpty() && !QFileInfo(dir).isDir()) {
        const QString up = QFileInfo(dir).absolutePath();
        if (up == dir) break;
        dir = up;
    }
    if (f.exists() && !f.isWritable()) return tr("The file is read-only.");
    if (QFileInfo(dir).isDir() && !QFileInfo(dir).isWritable())
        return tr("The folder %1 is read-only, or EmuLoader isn't allowed to write in it.").arg(QDir::toNativeSeparators(dir));
    const QStorageInfo disk(dir);
    if (disk.isValid() && disk.bytesAvailable() >= 0 && disk.bytesAvailable() < 1024 * 1024)
        return tr("The disk is full (%1 free).").arg(QLocale().formattedDataSize(disk.bytesAvailable()));
    return {};
}

QString FileProblem::cantSaveText(const QString& path) {
    const QString why = whyNotWritable(path);
    return tr("%1 couldn't be saved.").arg(QDir::toNativeSeparators(path)) + (why.isEmpty() ? QString() : ' ' + why);
}

void FileProblem::cantSave(QWidget* parent, const QString& title, const QString& path) {
    const QString why = whyNotWritable(path);
    QMessageBox box(QMessageBox::Warning, title, tr("%1 couldn't be saved.").arg(QFileInfo(path).fileName()), QMessageBox::Ok, parent);
    box.setObjectName("CantSaveMessage");
    box.setInformativeText((why.isEmpty() ? tr("Another program may be using it, or the disk may not be available.") : why) +
                           "\n\n" + tr("Check the file and its folder, then try again.") + "\n\n" + QDir::toNativeSeparators(path));
    box.exec();
}

void FileProblem::cantStart(QWidget* parent, const QString& title, const QString& program, const QString& reason) {
    QMessageBox box(QMessageBox::Warning, title, tr("%1 couldn't be started.").arg(QFileInfo(program).fileName()), QMessageBox::Ok, parent);
    box.setObjectName("CantStartMessage");
    box.setInformativeText((reason.isEmpty() ? QString() : tr("The reason given: %1").arg(reason) + "\n\n") +
                           tr("Make sure the file is still there, and that a security program isn't blocking it.") + "\n\n" +
                           QDir::toNativeSeparators(program));
    box.exec();
}

} // namespace eld
