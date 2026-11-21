#include "RomsFoldersDialog.h"

#include "VclRuntime.h"
#include "ui_FormArcadeROMsFolders.h"

#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>

namespace eld {

RomsFoldersDialog::RomsFoldersDialog(const Frontend& fe, SystemId current, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeROMsFolders), fe_(fe) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("MAME and Arcade ROMs Folders"));
    // SystemSelector (ELV_PopulateSystems): an icon per system; not set up = grayed out (SetSystemState)
    systems_ = new QListWidget(ui_->PanelSystemsSelect);
    systems_->setViewMode(QListView::IconMode);
    systems_->setIconSize(QSize(48, 48));
    systems_->setGridSize(QSize(96, 82));
    systems_->setMovement(QListView::Static);
    systems_->setFlow(QListView::LeftToRight);
    systems_->setFrameShape(QFrame::NoFrame);
    systems_->setFont(QFont("Verdana", 8));
    systems_->setStyleSheet("QListWidget { background: transparent; }");
    vcl::replaceDesigned(ui_->SystemSelector, systems_); // (in the form's place)
    const QString icons = fe_.resourcesFolder() + "main_icons/";
    int start = 0;
    for (SystemId id : {SystemId::MAME, SystemId::HBMAME}) {
        auto* it = new QListWidgetItem(QIcon(fe_.mainIcon(id == SystemId::MAME ? "emu_mame.ico" : "emu_hbmame.ico")),
                                       Frontend::section(id), systems_);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        if (!fe_.emulator(id).isSet()) {
            it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            it->setToolTip(tr("Not set up (Menu Options > MAME/Arcade > Emulators Setup)"));
        }
        if (id == current && fe_.emulator(id).isSet()) start = int(ids_.size());
        ids_ << id;
    }
    ui_->FoldersList->setReadOnly(true);
    ui_->FoldersList->setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(systems_, &QListWidget::currentRowChanged, this, &RomsFoldersDialog::showSystem);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    ui_->ButtonCancel->setDefault(true);
    systems_->setCurrentRow(start);
    showSystem(start);
    vcl::layoutForm(this); // every text whole in any language and size
}

RomsFoldersDialog::~RomsFoldersDialog() = default;

QString RomsFoldersDialog::shownText() const { return ui_->FoldersList->toPlainText(); }

// SystemSelectorItemSelectionChanged + LoadFoldersList
void RomsFoldersDialog::showSystem(int row) {
    if (row < 0 || row >= ids_.size()) return;
    const SystemId id = ids_[row];
    ui_->LabelSystemTitle->setText(Frontend::description(id, fe_.emulatorFile(id)).toUpper());
    QStringList lines;
    if (fe_.emulator(id).isSet())
        for (const QString& f : fe_.romFolders(id)) lines << QDir::toNativeSeparators(f);
    ui_->FoldersList->setPlainText(lines.join('\n'));
}

} // namespace eld
