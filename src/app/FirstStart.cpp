#include "FirstStart.h"

#include "Frontend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

namespace eld {

// The texts keep the "main" context (they were in main.cpp), so translations still match.
void askImportElsewhere(Frontend& fe) {
    while (true) {
        QMessageBox box(QMessageBox::Question, QStringLiteral(EL_PRODUCT_NAME),
                        QCoreApplication::translate("main",
                            "<p><b>Do you have EmuLoader, or an earlier copy of %1, installed in a different folder?</b></p>"
                            "<p>%1 can copy your settings, favorites, play history and custom parameters from it. "
                            "Its files are not changed.</p>"
                            "<p>Select its folder (the one with EmuLoader.exe or EmuLoader-ReLoaded.exe), or skip to start "
                            "fresh. This is only asked once.</p>").arg(QStringLiteral(EL_PRODUCT_NAME)));
        box.setTextFormat(Qt::RichText);
        QPushButton* select = box.addButton(QCoreApplication::translate("main", "Select Folder..."), QMessageBox::AcceptRole);
        box.addButton(QCoreApplication::translate("main", "Skip"), QMessageBox::RejectRole);
        box.setDefaultButton(select);
        box.exec();
        if (box.clickedButton() != select) break;
        QString dir = QFileDialog::getExistingDirectory(nullptr, QCoreApplication::translate("main", "Select the folder of EmuLoader or %1").arg(QStringLiteral(EL_PRODUCT_NAME)));
        if (dir.isEmpty()) continue;
        if (!Frontend::canImportFrom(dir)) {
            QMessageBox::warning(nullptr, QStringLiteral(EL_PRODUCT_NAME),
                                 QCoreApplication::translate("main", "There are no settings of EmuLoader or %1 in %2. Select the folder "
                                                                     "EmuLoader.exe or EmuLoader-ReLoaded.exe is in.")
                                     .arg(QStringLiteral(EL_PRODUCT_NAME), QDir::toNativeSeparators(dir)));
            continue;
        }
        if (!fe.importFrom(dir))
            QMessageBox::warning(nullptr, QStringLiteral(EL_PRODUCT_NAME),
                                 QCoreApplication::translate("main", "Could not save the settings in %1").arg(QDir::toNativeSeparators(fe.userDir())));
        break;
    }
    fe.saveIni(); // the settings file exists from now on: not asked again
}

} // namespace eld
