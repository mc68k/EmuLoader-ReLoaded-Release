// FileVersionsDialog - Menu Options > MAME/Arcade > Arcade Files Versions
// (TFormArcadeFileVersionsLarge): for each system that is set up, its emulator file, the
// emulator's version and the version of MAME its games list was made with.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <memory>

namespace Ui { class FormArcadeFileVersionsLarge; }

namespace eld {

class FileVersionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileVersionsDialog(const Frontend& fe, QWidget* parent = nullptr);
    ~FileVersionsDialog() override;
    int systemsShown() const { return shown_; }
    // [info_file] version of a games list (.el), "" when there's none.
    static QString gamesListVersion(const QString& elFile);

private:
    std::unique_ptr<Ui::FormArcadeFileVersionsLarge> ui_;
    int shown_ = 0;
};

} // namespace eld
