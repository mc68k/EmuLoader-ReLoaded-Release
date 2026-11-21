// GameFontsDialog - port of TFormGamesListFontSettings (uGamesListFontSettings.pas).
// Lists the five kinds of sets, each drawn in its own font; double-click to change
// the font, right-click for color and defaults.
#pragma once

#include "GameFonts.h"

#include <QDialog>
#include <memory>

namespace Ui { class FormGamesListFontSettings; }
class QStandardItemModel;

namespace eld {

class GameFontsDialog : public QDialog {
    Q_OBJECT
public:
    GameFontsDialog(const GameFonts& fonts, QWidget* parent = nullptr);
    ~GameFontsDialog() override;
    const GameFonts& fonts() const { return fonts_; }

private:
    void refresh();
    void changeFont(int row);
    void changeColor(int row);
    std::unique_ptr<Ui::FormGamesListFontSettings> ui_;
    GameFonts fonts_;
    QStandardItemModel* model_ = nullptr;
};

} // namespace eld
