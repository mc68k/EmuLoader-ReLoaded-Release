// MainWindow: .7z picture and icon archives. They're read as they are (ZipArchive / SevenZip);
// one that's solid in big blocks makes every picture slow to get, so EmuLoader offers once to
// convert it to a .zip beside it (Convert / Not Now / Don't Ask Again, kept per file in
// [SevenZip]), and Menu Options > Images > Convert .7z Picture Archives to .zip does it on demand.
// The .7z is kept unless the user ticks the box to delete it (then it goes to the Recycle Bin).
#include "MainWindow.h"
#include "ui_FormMain.h"

#include "DiagnosticLog.h"
#include "ProgressWindow.h"
#include "RecycleBin.h"
#include "SevenZip.h"
#include "SplashScreen.h"
#include "ZipArchive.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStorageInfo>
#include <QTimer>

namespace eld {

namespace {
QString zipOf(const QString& sevenZip) { return sevenZip.chopped(3) + ".zip"; }
} // namespace

// Every .7z the pictures and icons are read from (no .zip of the same name beside it)
QStringList MainWindow::sevenZipArchives() const {
    QStringList out;
    auto add = [&](const QString& f) {
        const QString abs = QFileInfo(f).absoluteFilePath();
        if (ZipArchive::sevenZipName(abs) && QFileInfo(abs).isFile() && !QFileInfo::exists(zipOf(abs)) && !out.contains(abs))
            out << abs;
    };
    for (int c = 0; c < ImageFinder::CountWithIcons; ++c)
        for (const QString& f : imageFinder_.zipFiles(c)) add(f);
    for (const QString& dir : fe_.iconFolders()) add(QDir(dir).filePath("icons.7z"));
    return out;
}

// atStart: only the slow ones not answered yet, and nothing said when there are none
void MainWindow::offerSevenZipConversion(bool atStart) {
    // at the start: once the splash screen has gone (it stays on top: the question would be behind it)
    if (atStart && (StartupSplash::active() || !isVisible())) {
        QTimer::singleShot(200, this, [this] { offerSevenZipConversion(true); });
        return;
    }
    struct Archive { QString file; qint64 unpacked = 0; bool slow = false; };
    QList<Archive> list;
    for (const QString& f : sevenZipArchives()) {
        if (atStart && fe_.ini().readString("SevenZip", QDir::toNativeSeparators(f)) == "never") continue;
        SevenZip sz;
        if (!sz.open(f)) continue;
        Archive a{f, 0, sz.slow()};
        for (const SevenZip::Entry& e : sz.entries()) a.unpacked += e.size;
        if (atStart && !a.slow) continue;
        list << a;
    }
    if (list.isEmpty()) {
        if (!atStart)
            QMessageBox::information(this, tr("Convert .7z Picture Archives to .zip"),
                                     tr("No .7z picture or icon archives were found in the picture and icon folders "
                                        "(or each already has a .zip of the same name beside it)."));
        return;
    }
    const QLocale loc;
    QStringList lines;
    for (const Archive& a : list)
        lines << QString("• %1 (%2%3)")
                     .arg(QDir::toNativeSeparators(a.file), loc.formattedDataSize(a.unpacked),
                          a.slow ? QString(", ") + tr("slow to read") : QString());
    QMessageBox box(QMessageBox::Question,
                    atStart ? tr("Slow .7z Picture Archives") : tr("Convert .7z Picture Archives to .zip"),
                    (atStart ? tr("These .7z archives are compressed in big solid blocks: to get one picture out, a lot "
                                  "of the archive has to be decompressed first, so their pictures are slow to show.")
                             : tr("These .7z archives can be converted to .zip files.")) +
                        "\n\n" + lines.join('\n') + "\n\n" +
                        tr("Converting makes a .zip beside each one, with the same name, from which every picture opens "
                           "at once. The size shown is about how big each .zip will be. The .7z files are kept unless "
                           "you choose to delete them."),
                    QMessageBox::NoButton, this);
    box.setObjectName("SevenZipConvertMessage");
    QPushButton* convert = box.addButton(tr("Convert"), QMessageBox::AcceptRole);
    box.addButton(tr("Not Now"), QMessageBox::RejectRole);
    QPushButton* never = atStart ? box.addButton(tr("Don't Ask Again"), QMessageBox::DestructiveRole) : nullptr;
    auto* del = new QCheckBox(tr("Delete the .7z files once their .zip files are checked (to the Recycle Bin)"));
    del->setObjectName("SevenZipDeleteAfter");
    box.setCheckBox(del);
    box.setDefaultButton(convert);
    box.exec();
    if (box.clickedButton() == never) {
        for (const Archive& a : list) fe_.ini().writeString("SevenZip", QDir::toNativeSeparators(a.file), "never");
        fe_.saveIni();
        DiagnosticLog::write("images", QString("7z: don't ask again for %1 archive(s)").arg(list.size()));
        return;
    }
    if (box.clickedButton() != convert) return;

    ProgressWindow win(tr("Convert .7z Picture Archives to .zip"), this);
    win.setCancelable(true);
    QStringList done, failed, toDelete;
    for (const Archive& a : list) {
        const QString zip = zipOf(a.file);
        const QString name = QFileInfo(a.file).fileName();
        const qint64 free = QStorageInfo(QFileInfo(a.file).absolutePath()).bytesAvailable();
        if (free >= 0 && free < a.unpacked + (64LL << 20)) {
            failed << tr("%1: not enough free space (%2 needed, %3 free)")
                          .arg(name, loc.formattedDataSize(a.unpacked), loc.formattedDataSize(free));
            continue;
        }
        ZipArchive::release(a.file); // (not held open by the picture panels while it's read)
        win.step(tr("Converting %1").arg(name), 1000);
        const QString err = convertSevenZipToZip(a.file, zip, [&](qint64 d, qint64 total) {
            win.setProgress(total > 0 ? int(d * 1000 / total) : 0,
                            tr("%1 of %2").arg(loc.formattedDataSize(d), loc.formattedDataSize(total)));
            return !win.canceled();
        });
        DiagnosticLog::write("images", QString("7z -> zip %1: %2").arg(QDir::toNativeSeparators(a.file), err.isEmpty() ? "done" : err));
        if (!err.isEmpty()) {
            failed << QString("%1: %2").arg(name, err == "stopped" ? tr("canceled") : err);
            if (win.canceled()) break;
            continue;
        }
        done << QDir::toNativeSeparators(zip);
        if (del->isChecked()) toDelete << a.file;
    }
    win.close();
    if (!toDelete.isEmpty()) {
        ZipArchive::release();
        moveToRecycleBin(toDelete, this);
    }
    if (!done.isEmpty()) { // the pictures and icons from the new zips
        ZipArchive::release();
        refreshImageFolders();
        mamu_.setFolders(fe_.iconFolders());
        refreshIcons();
    }
    QString msg;
    if (!done.isEmpty()) msg += tr("Converted:") + "\n" + done.join('\n');
    if (!failed.isEmpty()) msg += (msg.isEmpty() ? QString() : "\n\n") + tr("Not converted:") + "\n" + failed.join('\n');
    if (!msg.isEmpty()) QMessageBox::information(this, tr("Convert .7z Picture Archives to .zip"), msg);
}

} // namespace eld
