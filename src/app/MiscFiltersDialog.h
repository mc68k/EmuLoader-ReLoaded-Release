// MiscFiltersDialog - Miscellaneous Filters (the Misc tool bar button; TFormArcadeFiltersExtra,
// uArcadeFiltersGamesExtra.pas): BIOS, device and game ROMs, audio, controls, screen
// orientation, save state, cocktail mode, artwork, CHD and "no dump" sets, merged sets, CRC32
// collisions, the Neo-Geo MVS / ST-V multi-slot machines and the catver.ini categories to hide.
// Apply keeps the choices (see MiscFilters), Default puts everything back, Cancel forgets them.
#pragma once

#include "GameFilters.h"

#include <QDialog>
#include <QStringList>
#include <memory>

namespace Ui { class FormArcadeFiltersExtra; }
class QCheckBox;

namespace eld {

class MiscFiltersDialog : public QDialog {
    Q_OBJECT
public:
    // controlTypes: arcade\filters\mame_control_type.ini (name=title, in order); romsKnown: the list
    // has the ROM flags (made by this version), else the ROM filters are grayed out.
    MiscFiltersDialog(const MiscFilters& f, const QList<QPair<QString, QString>>& controlTypes, bool romsKnown,
                      QWidget* parent = nullptr);
    ~MiscFiltersDialog() override;

    MiscFilters values() const;

private:
    void show(const MiscFilters& f);
    void help();

    std::unique_ptr<Ui::FormArcadeFiltersExtra> ui_;
    QList<QPair<QString, QString>> controls_;
    QCheckBox* categories_[MiscFilters::CategoryCount] = {};
    MiscFilters start_;
};

} // namespace eld
