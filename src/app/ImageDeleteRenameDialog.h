// ImageDeleteRenameDialog - Delete Image / Rename Image (TFormImageDeleteRename,
// uImageDeleteRename.pas; image right-click menu): the picture with its file name, dimensions,
// size, date and type (and a warning when the type doesn't match the extension). Rename asks for
// the new name; the extension stays. Images inside zip files can't be renamed or deleted.
#pragma once

#include "ElDatabase.h"

#include <QDialog>
#include <QIcon>
#include <memory>

namespace Ui { class FormImageDeleteRename; }

namespace eld {

class ImageDeleteRenameDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        MameGame game;
        QIcon gameIcon;
        QString nameLine;      // the status bar's game name text
        QString system;        // "MAME", "HBMAME"
        QString file;          // the image
        QIcon categoryIcon;
        bool rename = false;
    };
    ImageDeleteRenameDialog(const Input& in, QWidget* parent = nullptr);
    ~ImageDeleteRenameDialog() override;

    // DeleteRenameCurrentImage: asks, then renames or deletes (to the Recycle Bin). True when done.
    static bool run(const Input& in, QWidget* parent);

    QString newName() const; // the new file name, with the image's extension

private:
    void accept() override;
    std::unique_ptr<Ui::FormImageDeleteRename> ui_;
    Input in_;
};

} // namespace eld
