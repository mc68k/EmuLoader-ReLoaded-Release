// Help > About: the program, its version and build (with Copy Version Info for problem reports),
// the links, the folders it uses and the credits.
#pragma once

#include <QDialog>
#include <QString>
#include <functional>

class QPushButton;

namespace eld {

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        QString icon;           // EmuLoader.ico
        QString mame;           // "MAME v0.289 (Nov 06 2026)", or "" when MAME isn't set up
        QString hbmame;         // the same for HBMAME
        QString dataFolder;     // the data folder (arcade, lang, ...)
        QString settingsFolder; // EmuLoaderRL
        QString licensesFolder; // the program's licenses folder
        std::function<void()> openGuide;
        std::function<void()> checkForUpdates;
    };
    AboutDialog(const Input& in, QWidget* parent = nullptr);

    QString versionInfo() const { return info_; } // what Copy Version Info copies
    static QString systemLine();                  // "Windows 11 (10.0.26100, x86_64)"

private:
    QString info_;
};

} // namespace eld
