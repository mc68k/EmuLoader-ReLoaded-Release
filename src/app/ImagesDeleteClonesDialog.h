// ImagesDeleteClonesDialog - Delete MAME and Arcade Clone Images (TFormImagesDeleteClones,
// uImagesDeleteClones.pas; Menu Options > Images > Delete Images of Clone Games): for one image
// category, Start Scan lists the clones of the games list that have their own pictures (outside
// zip files), with the clone's picture and its parent's side by side. Then:
//   Remove Selected (Delete key)  keeps those clones' pictures: they leave the list;
//   Rename to Parent              gives a clone's pictures to its parent (when the parent has
//                                 none of that name);
//   Delete All / Delete Selected  deletes the listed clones' pictures (to the Recycle Bin).
// The window's size and splitter are kept in el_extras.ini [ImagesDeleteClones].
#pragma once

#include "ElDatabase.h"
#include "ImageFinder.h"

#include <QDialog>
#include <QIcon>
#include <functional>
#include <memory>

namespace Ui { class FormImagesDeleteClones; }
class QStandardItemModel;
class QLabel;

namespace eld {

class ImagesDeleteClonesDialog : public QDialog {
    Q_OBJECT
public:
    struct Setup {
        QList<MameGame> games;                    // the games list as shown
        const ImageFinder* finder = nullptr;
        int category = 1;                         // the image panel's category
        QString system;                           // "MAME"
        QIcon systemIcon;
        std::function<QIcon(int category)> categoryIcon;
        std::function<QColor(int category)> background;
        std::function<QIcon(const MameGame&)> gameIcon;
        QString extrasIni;                        // el_extras.ini
    };
    ImagesDeleteClonesDialog(const Setup& setup, QWidget* parent = nullptr);
    ~ImagesDeleteClonesDialog() override;

    bool changed() const { return changed_; } // pictures were deleted or renamed

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void done(int r) override;

private:
    void scan();
    void showSelected();
    void removeSelected();
    void renameToParent();
    void deleteImages(bool selectedOnly);
    void selectCategory();
    void setCategory(int c);
    void updateCount();
    void showImage(QLabel* view, QLabel* caption, const QString& file, const QString& title);

    std::unique_ptr<Ui::FormImagesDeleteClones> ui_;
    Setup s_;
    QStandardItemModel* model_ = nullptr;
    int category_ = 1;
    bool changed_ = false;
};

} // namespace eld
