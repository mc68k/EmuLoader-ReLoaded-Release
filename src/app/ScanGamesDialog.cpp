#include "ScanGamesDialog.h"

#include "ElWidgets.h"
#include "VclRuntime.h"
#include "ui_FormArcadeScanGamesMode.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QRadioButton>
#include <QVBoxLayout>

namespace eld {

int ScanGamesDialog::mode(Frontend& fe) { return qBound(0, fe.ini().readInteger("Preferences", "ScanGamesMode", 0), 2); }
int ScanGamesDialog::setsMode(Frontend& fe) { return qBound(0, fe.ini().readInteger("Preferences", "ScanSetsModeMAME", 0), 2); }
bool ScanGamesDialog::chds(Frontend& fe) { return fe.ini().readBool("EmuLoaderRL", "AuditCHDs", true); }
bool ScanGamesDialog::samples(Frontend& fe) { return fe.ini().readBool("EmuLoaderRL", "AuditSamples", false); }

ScanGamesDialog::ScanGamesDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeScanGamesMode), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    Ui::FormArcadeScanGamesMode& u = *ui_;
    setWindowTitle(tr("Scan Games (Audit ROMs)"));
    Frontend& fe = *in_.fe;
    const QLocale loc;

    // Which Games: the form's software list box (its icon), with the choices
    u.MAMESoftwareListBox->setTitle(QStringLiteral("           ") + tr("Which Games"));
    u.ImageMAMESoftwareList->setPixmap(QIcon(fe.mainIcon("emu_mame.ico")).pixmap(16, 16));
    const QString texts[] = {tr("All Games"), tr("Available Games"), tr("Missing Games"), tr("Incomplete Games (Missing ROMs/CHDs)"),
                             tr("Selected Games")};
    const char* names[] = {"ScanAllGames", "ScanAvailableGames", "ScanMissingGames", "ScanIncompleteGames", "ScanSelectedGames"};
    for (int i = 0; i < 5; ++i) {
        auto* r = new QRadioButton(QStringLiteral("%1 (%2)").arg(texts[i], loc.toString(in_.counts[i])), u.MAMESoftwareListBox);
        r->setObjectName(names[i]);
        r->setFont(u.QuickScan->font());
        r->setEnabled(in_.counts[i] > 0);
        which_[i] = r;
    }
    (in_.counts[Selected] > 1 ? which_[Selected] : which_[All])->setChecked(true);

    // How
    u.ScanModeBox->setTitle(QStringLiteral("           ") + tr("How"));
    u.ScanModeIcon->setPixmap(QIcon(fe.mainIcon("scanresult_ok.ico")).pixmap(16, 16));
    u.ImageScanMAME->setPixmap(QIcon(fe.mainIcon("media_cartridge.ico")).pixmap(16, 16));
    u.LabelFullScan->setText(tr("(MAME checks every ROM: CRC32/SHA-1)"));
    u.LabelQuickScan->setText(tr("(only looks for the sets' files: .zip, .7z, CHD folders)"));
    u.ForceAllAvailable->setText(tr("Mark Games Available"));
    u.LabelForceAllAvailable->setText(tr("(no checking: they're shown as available)"));
    u.ScanMAMESetsBox->setTitle(QStringLiteral("           ") + tr("Scan MAME Sets"));
    checksLabel_ = new ElLabel(u.ScanModeBox);
    checksLabel_->setText(tr("Full Scan also checks:"));
    checksLabel_->setFont(u.LabelFullScan->font());
    checksLabel_->setPalette(u.LabelFullScan->palette());
    checksLabel_->setForegroundRole(u.LabelFullScan->foregroundRole());
    checksLabel_->setStyleSheet(u.LabelFullScan->styleSheet());
    chds_ = new QCheckBox(tr("CHDs"), u.ScanModeBox);
    samples_ = new QCheckBox(tr("Audio Samples"), u.ScanModeBox);
    chds_->setObjectName("AuditCHDs");
    samples_->setObjectName("AuditSamples");
    chds_->setToolTip(tr("Check the games' CHDs (hard disks, CDs, laserdiscs) too; a set missing only its CHD is incomplete"));
    samples_->setToolTip(tr("Check the audio samples of the games that play some (-verifysamples): listed in the results, "
                            "they don't change whether you have the game"));
    for (QCheckBox* c : {chds_, samples_}) c->setFont(u.FullScan->font());
    u.ScanMAMESetsBox->setVisible(in_.softwareLists);
    u.LabelImportantTips->setText(tr("Full Scan: MAME checks the sets, merged, split or non-merged. "
                                     "Creating the games list does a Quick Scan."));
    u.ButtonOk->setText(tr("Scan"));
    u.ButtonCancel->setText(tr("Cancel"));

    (mode(fe) == 1 ? u.QuickScan : mode(fe) == 2 ? u.ForceAllAvailable : u.FullScan)->setChecked(true);
    (setsMode(fe) == 1 ? u.ScanMAMEArcadeMachines : setsMode(fe) == 2 ? u.ScanMAMESoftwareListGames : u.ScanMAMEAllSets)->setChecked(true);
    chds_->setChecked(chds(fe));
    samples_->setChecked(samples(fe));
    for (QRadioButton* r : {u.FullScan, u.QuickScan, u.ForceAllAvailable})
        connect(r, &QRadioButton::toggled, this, [this] { updateEnabled(); });
    updateEnabled();
    layOut();
    connect(u.ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(u.ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    u.ButtonOk->setDefault(true);
}

ScanGamesDialog::~ScanGamesDialog() = default;

// In layouts: Which Games, then How (with its Full Scan checks and, for software list games, Scan
// MAME Sets), the tip, the buttons. Every text whole, in any language and size.
void ScanGamesDialog::layOut() {
    using vcl::boxed, vcl::loose;
    Ui::FormArcadeScanGamesMode& u = *ui_;
    // Which Games (the icon stays at its place in the box's title)
    QGroupBox* which = u.MAMESoftwareListBox;
    auto* w = boxed(new QVBoxLayout(which), 9, 10, 9, 6, 6);
    for (QRadioButton* r : which_) {
        loose(r);
        w->addWidget(r);
    }
    // How
    QGroupBox* how = u.ScanModeBox;
    auto* h = boxed(new QVBoxLayout(how), 9, 10, 9, 6, 6);
    const std::pair<QRadioButton*, QLabel*> modes[] = {{u.FullScan, u.LabelFullScan}, {u.QuickScan, u.LabelQuickScan},
                                                       {u.ForceAllAvailable, u.LabelForceAllAvailable}};
    for (auto [r, l] : modes) {
        auto* row = boxed(new QHBoxLayout, 0, 0, 0, 0, 6);
        loose(r);
        loose(l);
        row->addWidget(r);
        row->addWidget(l);
        row->addStretch();
        h->addLayout(row);
    }
    auto* checks = boxed(new QHBoxLayout, 19, 0, 0, 0, 10);
    loose(checksLabel_);
    checks->addWidget(checksLabel_);
    for (QCheckBox* c : {chds_, samples_}) {
        loose(c);
        checks->addWidget(c);
    }
    checks->addStretch();
    h->addLayout(checks);
    if (in_.softwareLists) {
        QGroupBox* sets = u.ScanMAMESetsBox;
        auto* r = boxed(new QHBoxLayout(sets), 2, 6, 2, 2, 12);
        for (QRadioButton* b : {u.ScanMAMEAllSets, u.ScanMAMEArcadeMachines, u.ScanMAMESoftwareListGames}) {
            loose(b);
            r->addWidget(b);
        }
        r->addStretch();
        loose(sets);
        sets->setProperty("vclLaidOut", true);
        h->addSpacing(2);
        h->addWidget(sets);
    }
    // the tip
    loose(u.LabelImportantTips);
    u.LabelImportantTips->setWordWrap(true);
    u.LabelImportantTips->setMinimumWidth(340); // (its lines reckoned at about the design's width)
    // Scan, Cancel
    QWidget* bottom = u.PanelBottom;
    loose(bottom);
    auto* b = boxed(new QHBoxLayout(bottom), 8, 8, 8, 8, 15);
    b->addStretch();
    b->addWidget(vcl::textButton(u.ButtonOk));
    b->addWidget(vcl::textButton(u.ButtonCancel));
    b->addStretch();

    auto* middle = boxed(new QVBoxLayout, 8, 8, 8, 6, 10);
    for (QGroupBox* g : {which, how}) {
        loose(g);
        middle->addWidget(g);
    }
    auto* tip = boxed(new QHBoxLayout, 9, 0, 9, 0, 0);
    tip->addWidget(u.LabelImportantTips);
    middle->addLayout(tip);
    middle->addStrut(364); // (the design's width at least)
    auto* root = boxed(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addLayout(middle);
    root->addWidget(bottom);
    root->setSizeConstraint(QLayout::SetFixedSize);
    for (QWidget* x : {(QWidget*)this, (QWidget*)which, (QWidget*)how, bottom}) x->setProperty("vclLaidOut", true);
}

void ScanGamesDialog::updateEnabled() {
    const bool full = ui_->FullScan->isChecked();
    chds_->setEnabled(full);
    samples_->setEnabled(full);
    checksLabel_->setEnabled(full);
}

ScanGamesDialog::Which ScanGamesDialog::which() const {
    for (int i = 0; i < 5; ++i)
        if (which_[i]->isChecked()) return Which(i);
    return All;
}

bool ScanGamesDialog::canChoose(Which w) const { return which_[w]->isEnabled(); }

void ScanGamesDialog::choose(Which which, int mode, bool chds, bool samples, int sets) {
    which_[which]->setChecked(true);
    (mode == 1 ? ui_->QuickScan : mode == 2 ? ui_->ForceAllAvailable : ui_->FullScan)->setChecked(true);
    chds_->setChecked(chds);
    samples_->setChecked(samples);
    if (sets >= 0) (sets == 1 ? ui_->ScanMAMEArcadeMachines : sets == 2 ? ui_->ScanMAMESoftwareListGames : ui_->ScanMAMEAllSets)->setChecked(true);
}

void ScanGamesDialog::accept() {
    WinIni& ini = in_.fe->ini();
    ini.writeInteger("Preferences", "ScanGamesMode", ui_->QuickScan->isChecked() ? 1 : ui_->ForceAllAvailable->isChecked() ? 2 : 0);
    if (in_.softwareLists)
        ini.writeInteger("Preferences", "ScanSetsModeMAME", ui_->ScanMAMEArcadeMachines->isChecked() ? 1 : ui_->ScanMAMESoftwareListGames->isChecked() ? 2 : 0);
    ini.writeInteger("EmuLoaderRL", "AuditCHDs", chds_->isChecked() ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "AuditSamples", samples_->isChecked() ? 1 : 0);
    QDialog::accept();
}

} // namespace eld
