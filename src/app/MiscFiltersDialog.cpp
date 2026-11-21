#include "MiscFiltersDialog.h"

#include "VclRuntime.h"
#include "ui_FormArcadeFiltersExtra.h"

#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>

namespace eld {

MiscFiltersDialog::MiscFiltersDialog(const MiscFilters& f, const QList<QPair<QString, QString>>& controlTypes,
                                     bool romsKnown, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeFiltersExtra), controls_(controlTypes), start_(f) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Miscellaneous Filters"));

    // The left column's check boxes take the room up to the middle column.
    for (QCheckBox* c : {ui_->NeoGeoMVS, ui_->STVMultiSlot, ui_->HideMAMESoftlist_vgmplay, ui_->HideGamesWithCHDFiles,
                         ui_->HideNoDumpROMsGames, ui_->ShowMergedSetsOnly, ui_->ShowOnlySetsCRC32Collision,
                         ui_->FilterGamesMainCPU})
    {
        vcl::place(c, QRect(c->x(), c->y(), ui_->LabelCocktail->x() - c->x() - 8, c->height()));
    }

    QCheckBox* cats[] = {ui_->CategoryCasino, ui_->CategoryFruitMachines, ui_->CategorySlotMachines, ui_->CategoryRhythm,
                         ui_->CategoryMature, ui_->CategoryMahjong, ui_->CategoryTabletop, ui_->CategoryPinMAME,
                         ui_->CategoryBoardGame, ui_->CategoryQuiz, ui_->CategoryUtilities, ui_->CategoryCalculator,
                         ui_->CategoryEducational, ui_->CategoryElectronic, ui_->CategoryPrinters, ui_->CategoryPhones,
                         ui_->CategoryMusic, ui_->CategoryMAMEConsoleMachines, ui_->CategoryMAMEComputerMachines,
                         ui_->CategoryMAMEHandheldMachines};
    for (int c = 0; c < MiscFilters::CategoryCount; ++c) categories_[c] = cats[c];

    // MAME Control Type: "List All", then mame_control_type.ini's titles.
    for (const auto& c : controls_) ui_->ControlType_New->addItem(c.second);

    ui_->FilterGamesMainCPU->setToolTip(tr("A row under the tool bar to show MAME's games by their main CPU"));
    // The ROM filters need a games list made by this version.
    if (!romsKnown) {
        const QString again = tr("Create the games list again (Menu Options > MAME/Arcade > Create Arcade Games List) "
                                 "to use this filter.");
        for (QWidget* w : {(QWidget*)ui_->GamesROMs, (QWidget*)ui_->LabelGamesROMs, (QWidget*)ui_->DeviceROMs,
                           (QWidget*)ui_->LabelDeviceROMs, (QWidget*)ui_->HideNoDumpROMsGames,
                           (QWidget*)ui_->ShowOnlySetsCRC32Collision}) {
            w->setEnabled(false);
            w->setToolTip(again);
        }
    }
    ui_->LabelCategoryFiltersGroupBoxHint->setText(tr("catver.ini required"));

    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui_->ButtonDefaultOptions, &QPushButton::clicked, this, [this] {
        MiscFilters d;
        d.mainCpu = start_.mainCpu;
        show(d);
    });
    connect(ui_->ButtonCategoriesToHideInfo, &QPushButton::clicked, this, &MiscFiltersDialog::help);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ButtonOk->setDefault(true);
    show(f);
    // rows and columns from the design: as big as the texts need (a translation, a larger font)
    vcl::layoutForm(this);
}

MiscFiltersDialog::~MiscFiltersDialog() = default;

void MiscFiltersDialog::show(const MiscFilters& f) {
    auto set = [](QComboBox* c, int i) { c->setCurrentIndex(std::clamp(i, 0, c->count() - 1)); };
    set(ui_->Bios, f.bios);
    set(ui_->DeviceROMs, f.deviceRoms);
    set(ui_->GamesROMs, f.gameRoms);
    set(ui_->AudioType, f.audio);
    set(ui_->InterfaceType, f.interface);
    set(ui_->ScreenOrientation, f.orientation);
    set(ui_->SaveState, f.saveState);
    set(ui_->SpecialList, f.specialList);
    set(ui_->Cocktail, f.cocktail);
    set(ui_->ArtworkRequired, f.artwork);
    int ctrl = 0;
    for (int i = 0; i < controls_.size(); ++i)
        if (controls_[i].first.compare(f.control, Qt::CaseInsensitive) == 0) ctrl = i + 1;
    ui_->ControlType_New->setCurrentIndex(ctrl);
    ui_->HideBiosSets->setChecked(f.hideBiosSets);
    ui_->HideDeviceSets->setChecked(f.hideDeviceSets);
    ui_->HideGamesWithCHDFiles->setChecked(f.hideChd);
    ui_->HideNoDumpROMsGames->setChecked(f.hideNoDump);
    ui_->NeoGeoMVS->setChecked(f.neoGeoMvs);
    ui_->STVMultiSlot->setChecked(f.stvMultiSlot);
    ui_->ShowMergedSetsOnly->setChecked(f.mergedOnly);
    ui_->ShowOnlySetsCRC32Collision->setChecked(f.crcCollisionOnly);
    ui_->HideMAMESoftlist_vgmplay->setChecked(f.hideVgmplay);
    ui_->FilterGamesMainCPU->setChecked(f.mainCpu);
    for (int c = 0; c < MiscFilters::CategoryCount; ++c) categories_[c]->setChecked(f.show[c]);
}

MiscFilters MiscFiltersDialog::values() const {
    MiscFilters f;
    f.bios = ui_->Bios->currentIndex();
    f.deviceRoms = ui_->DeviceROMs->currentIndex();
    f.gameRoms = ui_->GamesROMs->currentIndex();
    f.audio = ui_->AudioType->currentIndex();
    f.interface = ui_->InterfaceType->currentIndex();
    f.orientation = ui_->ScreenOrientation->currentIndex();
    f.saveState = ui_->SaveState->currentIndex();
    f.specialList = ui_->SpecialList->currentIndex();
    f.cocktail = ui_->Cocktail->currentIndex();
    f.artwork = ui_->ArtworkRequired->currentIndex();
    int ctrl = ui_->ControlType_New->currentIndex();
    f.control = ctrl > 0 && ctrl <= controls_.size() ? controls_[ctrl - 1].first : QString();
    f.hideBiosSets = ui_->HideBiosSets->isChecked();
    f.hideDeviceSets = ui_->HideDeviceSets->isChecked();
    f.hideChd = ui_->HideGamesWithCHDFiles->isChecked();
    f.hideNoDump = ui_->HideNoDumpROMsGames->isChecked();
    f.neoGeoMvs = ui_->NeoGeoMVS->isChecked();
    f.stvMultiSlot = ui_->STVMultiSlot->isChecked();
    f.mergedOnly = ui_->ShowMergedSetsOnly->isChecked();
    f.crcCollisionOnly = ui_->ShowOnlySetsCRC32Collision->isChecked();
    f.hideVgmplay = ui_->HideMAMESoftlist_vgmplay->isChecked();
    f.mainCpu = ui_->FilterGamesMainCPU->isChecked();
    for (int c = 0; c < MiscFilters::CategoryCount; ++c) f.show[c] = categories_[c]->isChecked();
    return f;
}

// ButtonCategoriesToHideInfoClick
void MiscFiltersDialog::help() {
    QMessageBox box(QMessageBox::Information, tr("Help"),
                    tr("<p>To use these filters, put <b>catver.ini</b> from AntoPISA in the <b>arcade\\filters</b> "
                       "folder, then restart EmuLoader to load it into the games list.</p>"
                       "<p>You can find catver.ini at AntoPISA's Progetto Snaps: https://www.progettosnaps.net/catver/</p>"
                       "<p>A game is hidden when its category has the words of an unchecked box (\"Casino\", "
                       "\"Mature\" or \"Adult\" ...). <b>MAME Console Machines</b>, <b>MAME Computer Machines</b> and "
                       "<b>MAME Handheld Machines</b> show or hide MAME machines only; software lists are not "
                       "included.</p>"),
                    QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.exec();
}

} // namespace eld
