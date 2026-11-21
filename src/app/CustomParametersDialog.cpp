#include "CustomParametersDialog.h"
#include "FileProblem.h"

#include "ElWidgets.h"
#include "RunExtras.h"
#include "VclRuntime.h"
#include "ui_FormCustomParameters.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace eld {

namespace {
const char* kIniSection = "Preferences";
const char* kIniPanel = "CustomParameters_ShowMachinesPanel";
} // namespace

CustomParametersDialog::CustomParametersDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormCustomParameters), fe_(fe), sys_(sys), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    const MameGame& g = in_.game;
    softwareLines_ = g.isSoftware() && in_.what != Machine;
    switch (in_.what) {
    case Parent: setWindowTitle(tr("Parent Game Custom Parameters")); break;
    case SoftwareList: setWindowTitle(tr("Software List Custom Parameters")); break;
    case Machine: setWindowTitle(tr("Machine Custom Parameters")); break;
    default: setWindowTitle(in_.parent ? tr("Parent Game Custom Parameters") : tr("Game Custom Parameters")); break;
    }

    // Top bar (FormShow): the game's icon, title, name line and MAME's version.
    static const int topColors[] = {0xF0FAE5, 0xE5F0FA, 0xEEEEEE};
    ui_->TopBar->setProperty("vclColor1", ElColorBox::toQColor(topColors[std::clamp<int>(g.setStatus, 0, 2)]).name());
    ui_->TopBar->setProperty("vclColor2", QColor(0xF1F1F1).name());
    ui_->SystemIcon->setPixmap(in_.gameIcon.pixmap(68, 68));
    ui_->SystemIcon->setAlignment(Qt::AlignCenter);
    ui_->GameIcon->setPixmap(QIcon(in_.emuIcon).pixmap(24, 24));
    ui_->LabelGameTitle->setText(in_.what == SoftwareList ? g.softwareTitle : g.title);
    QString name = g.cloneOf.isEmpty() ? tr("name: %1").arg(g.name) : tr("name: %1 [clone of %2]").arg(g.name, g.cloneOf);
    if (in_.what == Machine) name = tr("machine %1").arg(name);
    if (!g.bios.isEmpty()) name += " " + tr("[bios %1]").arg(g.bios);
    if (g.isSoftware()) name += " " + tr("[software %1]").arg(g.softwareList);
    if (g.merged) name += " " + tr("[merged]");
    if (in_.what == SoftwareList) name = tr("software name: %1").arg(g.softwareList);
    QString version = fe_.emulatorVersion(sys_);
    if (g.isSoftware()) {
        // the list, and what it's compatible with or the machine it runs with
        QString second = in_.what == SoftwareList ? QString() : tr("software list: %1").arg(g.softwareTitle);
        QString third = g.compat;
        for (const MameGame& m : in_.machines)
            if (m.name == in_.machine) third = tr("run with machine [%1]: %2").arg(m.name, m.title);
        ui_->LabelEmulatorVersion->setText(QStringList{name, second, third}.filter(QRegularExpression(".")).join('\n'));
    } else {
        ui_->LabelEmulatorVersion->setText(name + (version.isEmpty() ? QString() : "\n" + version));
    }

    // The software list lines: how to use the game, and the requirement the frontend adds.
    ui_->SoftwareUsageTitle->setText(tr("Usage instructions:"));
    ui_->SoftwareRequirementTitle->setText(tr("Requirement (added by the frontend):"));
    ui_->SoftwareUsageText->setText(in_.what == SoftwareList ? QString() : g.usage);
    ui_->SoftwareRequirementText->setText(in_.requirement);
    for (QLabel* text : {(QLabel*)ui_->SoftwareUsageText, (QLabel*)ui_->SoftwareRequirementText}) text->setToolTip(text->text());
    for (QWidget* w : {(QWidget*)ui_->SoftwareUsageTitle, (QWidget*)ui_->SoftwareUsageText,
                       (QWidget*)ui_->SoftwareRequirementTitle, (QWidget*)ui_->SoftwareRequirementText})
        w->setVisible(softwareLines_);

    // The tips: the colored words were labels laid over gaps in the text; one rich text label
    // each here, so translations can put the words anywhere.
    auto span = [](const QString& color, const QString& t) {
        return QString("<span style='color:%1'>%2</span>").arg(color, t.toHtmlEscaped());
    };
    ui_->LabelTips1->setTextFormat(Qt::RichText);
    ui_->LabelTips1->setWordWrap(true);
    ui_->LabelTips1->setText(
        tr("The %1 parameter is optional, used with software lists. It's added by the frontend at the end in "
           "slot1/drive1 (when emulator is running). Useful to load the game in another slot, but it must be the "
           "last argument %2.")
            .arg(span("green", tr("media")), span("green", "(-cart1  -cart2  -cass  -flop2  others)").replace(" ", "&nbsp;").replace("<span&nbsp;", "<span ")));
    ui_->LabelTips2->setTextFormat(Qt::RichText);
    ui_->LabelTips2->setWordWrap(true);
    ui_->LabelTips2->setText(tr("You may use a %1 tag, it will be replaced by the game name (it then isn't put "
                                "before the parameters).")
                                 .arg(span("maroon", "<game>")));

    ui_->CustomParametersGroupBoxLabel->setText(
        tr("Custom Parameters") + "   " +
        tr("(for software lists, you can enter a machine name before the parameters to overwrite current selected machine)"));
    ui_->CustomParametersSamplesGroupBoxLabel->setText(tr("Custom Parameters Samples"));

    // AddParamSamples
    struct Sample { const char* title; const char* params; };
    static const Sample samples[] = {
        {QT_TR_NOOP("TI-99/4A cartridge game (ti99_cart.xml software) with arguments to attach the speech synthesizer"),
         "-ioport peb -ioport:peb:slot3 speech"},
        {QT_TR_NOOP("Intellivision cartridge game (intv.xml software) with arguments to attach the Intellivoice Voice Synthesis Module"),
         "-cart1 voice -cart2"},
        {QT_TR_NOOP("MSX cartridge game (msx1_cart.xml software) with arguments to overwrite selected MAME machine and insert the cartridge in slot #2"),
         "fsa1wsx -cart2"},
        {QT_TR_NOOP("MSX cartridge game (msx1_cart.xml software) with arguments to attach the MoonSound cartridge and insert the cartridge in slot #2"),
         "-cartslot1 moonsound -cart2"},
        {QT_TR_NOOP("Commodore 64 floppy game (c64_flop_misc.xml software) with arguments to load and run the game without typing commands"),
         "-autoboot_delay \"2\" -autoboot_command \"LOAD\\\"ARTHUR\\\",8\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\n\\nRUN\\n\""},
        {QT_TR_NOOP("Atari 800 machine with arguments to attach a cassette tape player"), "-sio cassette"},
    };
    QString html;
    for (const Sample& s : samples)
        html += QString("<p style='margin:0 0 10px 0'>&nbsp;&nbsp;&nbsp;&nbsp;%1<br><span style='font-family:Consolas,"
                        "\"DejaVu Sans Mono\",monospace'>%2</span></p>")
                    .arg(tr(s.title).toHtmlEscaped(), QString(s.params).toHtmlEscaped());
    ui_->CustomParametersSamples->setHtml(html);
    ui_->CustomParametersSamples->setFrameShape(QFrame::NoFrame);

    // Bottom
    ui_->LabelCustomParamFile->setText(QDir::toNativeSeparators(in_.file));
    ui_->LabelCustomParamFile->setToolTip(QDir::toNativeSeparators(in_.file));
    ui_->ButtonReload->setText(tr("Reload"));
    ui_->ButtonReload->setToolTip(tr("Load settings from file"));
    ui_->ButtonOk->setText(tr("OK"));
    ui_->ButtonOk->setToolTip(tr("Close and save settings"));
    ui_->ButtonCancel->setText(tr("Cancel"));
    ui_->ButtonCancel->setToolTip(tr("Close without saving"));
    connect(ui_->ButtonReload, &QPushButton::clicked, this, &CustomParametersDialog::reload);
    connect(ui_->ButtonOk, &QPushButton::clicked, this, &CustomParametersDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ButtonOk->setDefault(true);

    // Machine Slots / Media Info
    ui_->PanelMachineSlotsGroupBoxLabel->setText(tr("Machine Slots / Media Info"));
    machines_ = new QStandardItemModel(0, 5, this);
    machines_->setHorizontalHeaderLabels({tr("Machine"), tr("Name"), tr("Hz"), tr("Clone of"), tr("Save State")});
    QTreeView* list = ui_->MachinesListView;
    list->setModel(machines_);
    list->setRootIsDecorated(false);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list->setSelectionBehavior(QAbstractItemView::SelectRows);
    list->setIconSize(QSize(16, 16));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int widths[] = {310, 110, 81, 110}; // Hz wide enough for "59.92 Hz"
    for (int c = 0; c < 4; ++c) list->setColumnWidth(c, widths[c]);
    {
        // PopulateMachinesList: a software list game's machines, else the game (or machine) itself
        auto add = [&](const MameGame& m, const QIcon& icon, bool runsWith) {
            QString hz = m.refresh;
            bool ok = false;
            double v = hz.toDouble(&ok);
            if (ok) hz = QString::number(v, 'f', 2) + " Hz";
            QString ss = m.saveState == 1 ? tr("Supported") : m.saveState == 0 ? tr("Unsupported") : QString();
            QList<QStandardItem*> row{new QStandardItem(icon, m.title), new QStandardItem(m.name),
                                      new QStandardItem(hz), new QStandardItem(m.cloneOf), new QStandardItem(ss)};
            QFont bold = list->font();
            bold.setBold(true);
            if (runsWith)
                for (QStandardItem* it : row) it->setFont(bold); // the machine the game runs with
            machines_->appendRow(row);
            if (runsWith) list->setCurrentIndex(machines_->index(machines_->rowCount() - 1, 0));
        };
        if (g.isSoftware() || (in_.what == Machine && !in_.machines.isEmpty())) {
            QVector<MameGame> ms = in_.machines;
            std::sort(ms.begin(), ms.end(), [](const MameGame& a, const MameGame& b) {
                return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
            });
            const QString runs = in_.what == Machine ? g.name : in_.machine;
            for (const MameGame& m : ms) add(m, in_.machineIcon ? in_.machineIcon(m) : QIcon(), m.name == runs);
            if (!list->currentIndex().isValid() && machines_->rowCount()) list->setCurrentIndex(machines_->index(0, 0));
            connect(list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { showSlotsMedia(); });
        } else {
            add(g, in_.gameIcon, true);
        }
    }
    ui_->MachinesSlotsRadioButton->setText(tr("Slots"));
    ui_->MachinesSlotsRadioButton->setToolTip(tr("Show slots options for the selected machine"));
    ui_->MachinesMediaRadioButton->setText(tr("Media"));
    ui_->MachinesMediaRadioButton->setToolTip(tr("Show media options for the selected machine"));
    ui_->MachinesSlotsRadioButton->setChecked(true);
    ui_->MachineSlotsRunCommandLabel->setText(tr("Edit Command"));
    ui_->MachineSlotsRunCommandButtonRun->setText(tr("Run Command"));
    ui_->MachineSlotsRunCommand->setText("-listslots");
    ui_->MachineSlotsInfo->setFrameShape(QFrame::NoFrame);
    ui_->MachineSlotsInfo->setLineWrapMode(QTextEdit::NoWrap);
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(13);
    ui_->MachineSlotsInfo->setFont(mono);
    ui_->MachineSlotsInfoTitleLabel->setFont(mono);
    ui_->MachineSlotsInfoTitleLabel->setText({});
    connect(ui_->MachinesSlotsRadioButton, &QRadioButton::toggled, this, [this] { showSlotsMedia(); });
    connect(ui_->MachineSlotsRunCommandButtonRun, &QPushButton::clicked, this,
            [this] { runCommand(ui_->MachineSlotsRunCommand->text()); });
    connect(ui_->MachineSlotsRunCommand, &QLineEdit::returnPressed, this,
            [this] { runCommand(ui_->MachineSlotsRunCommand->text()); });
    ui_->ButtonMachinesPanelShowHide->setToolTip(tr("Show/hide machine slots/media info panel"));
    connect(ui_->ButtonMachinesPanelShowHide, &QPushButton::clicked, this, [this] { setPanelVisible(!panel_); });

    layoutWindow();
    reload();
    setPanelVisible(fe_.ini().readInteger(kIniSection, kIniPanel, 1) != 0);
    ui_->CustomParameters_Edit->setFocus();
    // MAME is asked once the window is up.
    QTimer::singleShot(0, this, [this] { showSlotsMedia(); });
}

CustomParametersDialog::~CustomParametersDialog() = default;

namespace {
// A form's control put in a layout: no design size limits, its whole text.
void loose(QWidget* w, bool elide = false) {
    if (!elide) vcl::stopEliding(w);
    else vcl::setDataLabel(w); // (data, shortened when long)
    w->setMinimumSize(0, 0);
    w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    // (texts, buttons and edit boxes: as tall as they need, the text boxes take the extra room)
    const bool tall = qobject_cast<QAbstractScrollArea*>(w) || w->inherits("eld::ElShape") || w->inherits("eld::ElPanel");
    w->setSizePolicy(QSizePolicy::Preferred, tall ? QSizePolicy::Preferred : QSizePolicy::Fixed);
}
// A button: its design width at least, wider for a longer text.
QPushButton* button(QPushButton* b) {
    const int design = b->width();
    loose(b);
    b->setMinimumWidth(std::max(design, b->sizeHint().width()));
    return b;
}
QBoxLayout* box(QBoxLayout* l, int left, int top, int right, int bottom, int spacing) {
    l->setContentsMargins(left, top, right, bottom);
    l->setSpacing(spacing);
    return l;
}
// A titled panel (a title bar on top, the contents 8 pixels in) laid out by its layout
QVBoxLayout* titled(QWidget* panel, QLabel* title) {
    panel->setProperty("vclLaidOut", true);
    loose(panel);
    loose(title);
    auto* v = new QVBoxLayout(panel);
    box(v, 1, 1, 1, 10, 6);
    v->addWidget(title);
    auto* in = new QVBoxLayout;
    box(in, 7, 0, 7, 0, 8);
    v->addLayout(in, 1);
    return in;
}
// A thin frame around a text box
QWidget* framed(QWidget* frame, const QList<QWidget*>& inside, int margin) {
    frame->setProperty("vclLaidOut", true);
    loose(frame);
    auto* v = new QVBoxLayout(frame);
    box(v, margin, 1, margin, margin, 0);
    for (QWidget* w : inside) {
        loose(w);
        v->addWidget(w, w == inside.last() ? 1 : 0);
    }
    return frame;
}
} // namespace

// The window in layouts: the top bar; the parameters, the tips and the samples on the left with
// the file and the buttons under them; the Machine Slots / Media Info panel on the right. Longer
// translations and larger text get the room they need, and the window can be made bigger.
void CustomParametersDialog::layoutWindow() {
    vcl::autoLayout(ui_->TopBar);
    {   // (the bar's design is the whole window wide: past its texts, no margin of that size)
        QMargins m = ui_->TopBar->layout()->contentsMargins();
        m.setRight(8);
        ui_->TopBar->layout()->setContentsMargins(m);
    }
    // the software list lines (a software list game only)
    auto* soft = new QGridLayout;
    soft->setContentsMargins(0, 0, 0, 0);
    soft->setHorizontalSpacing(6);
    soft->setVerticalSpacing(0);
    soft->addWidget(ui_->SoftwareUsageTitle, 0, 0);
    soft->addWidget(ui_->SoftwareUsageText, 0, 1);
    soft->addWidget(ui_->SoftwareRequirementTitle, 1, 0);
    soft->addWidget(ui_->SoftwareRequirementText, 1, 1);
    soft->setColumnStretch(1, 1);
    for (QLabel* l : {(QLabel*)ui_->SoftwareUsageTitle, (QLabel*)ui_->SoftwareRequirementTitle}) loose(l);
    for (QLabel* l : {(QLabel*)ui_->SoftwareUsageText, (QLabel*)ui_->SoftwareRequirementText}) {
        loose(l, true); // (shortened, whole in the tooltip)
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    // Custom Parameters
    titled(ui_->CustomParametersGroupBox, ui_->CustomParametersGroupBoxLabel)->addWidget(ui_->CustomParameters_Edit);
    loose(ui_->CustomParameters_Edit);
    ui_->CustomParametersGroupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // the tips, the panel's show / hide button at their right
    for (QLabel* l : {(QLabel*)ui_->LabelTips1, (QLabel*)ui_->LabelTips2}) {
        loose(l);
        l->setMinimumWidth(720); // (the left side's width less the button: so their height is reckoned at it)
    }
    auto* tips = box(new QVBoxLayout, 0, 0, 0, 0, 5);
    tips->addWidget(ui_->LabelTips1);
    tips->addWidget(ui_->LabelTips2);
    auto* tipsRow = box(new QHBoxLayout, 7, 0, 0, 0, 12);
    tipsRow->addLayout(tips, 1);
    loose(ui_->ButtonMachinesPanelShowHide);
    ui_->ButtonMachinesPanelShowHide->setFixedSize(18, 31);
    tipsRow->addWidget(ui_->ButtonMachinesPanelShowHide, 0, Qt::AlignBottom);
    // the samples
    titled(ui_->CustomParametersSamplesGroupBox, ui_->CustomParametersSamplesGroupBoxLabel)
        ->addWidget(framed(ui_->CustomParametersSamplesFrame, {ui_->CustomParametersSamples}, 1), 1);
    ui_->CustomParametersSamples->setMinimumHeight(120);
    // the file, Reload, OK and Cancel
    QWidget* bottom = ui_->PanelBottom;
    bottom->setProperty("vclLaidOut", true);
    loose(bottom);
    bottom->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* b = box(new QHBoxLayout(bottom), 0, 8, 0, 8, 8);
    b->addWidget(button(ui_->ButtonReload));
    loose(ui_->LabelCustomParamFile, true); // (a path: shortened, whole in the tooltip)
    ui_->LabelCustomParamFile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    b->addWidget(ui_->LabelCustomParamFile, 1);
    b->addWidget(button(ui_->ButtonOk));
    b->addWidget(button(ui_->ButtonCancel));

    auto* left = box(new QVBoxLayout, 0, 0, 0, 0, 0);
    left->addStrut(763); // (the left side's design width, at least)
    if (softwareLines_) {
        left->addLayout(soft);
        left->addSpacing(8);
    } else {
        delete soft;
    }
    left->addWidget(ui_->CustomParametersGroupBox);
    left->addSpacing(13);
    left->addLayout(tipsRow);
    left->addSpacing(12);
    left->addWidget(ui_->CustomParametersSamplesGroupBox, 1);
    left->addSpacing(12);
    left->addWidget(bottom);
    for (QWidget* w : {(QWidget*)ui_->SoftwareUsageTitle, (QWidget*)ui_->SoftwareUsageText,
                       (QWidget*)ui_->SoftwareRequirementTitle, (QWidget*)ui_->SoftwareRequirementText})
        w->setVisible(softwareLines_);

    // Machine Slots / Media Info
    QWidget* panel = ui_->PanelMachineSlotsGroupBox;
    QVBoxLayout* p = titled(panel, ui_->PanelMachineSlotsGroupBoxLabel);
    p->addStrut(714); // (the panel's design width, 730, at least)
    QTreeView* v = ui_->MachinesListView;
    loose(v);
    // AdjustMachinesPanel: the machines list as high as its rows (up to 6)
    const int rows = std::clamp<int>(machines_->rowCount(), 1, 6);
    const int rowH = std::max(v->sizeHintForRow(0), 20);
    v->setFixedHeight(std::min(154, v->header()->sizeHint().height() + rowH * rows + 4 + 2 * v->frameWidth()));
    p->addWidget(v);
    auto* cmd = box(new QHBoxLayout, 0, 0, 0, 0, 8);
    for (QWidget* w : {(QWidget*)ui_->MachinesSlotsRadioButton, (QWidget*)ui_->MachinesMediaRadioButton,
                       (QWidget*)ui_->MachineSlotsRunCommandLabel, (QWidget*)ui_->MachineSlotsRunCommand}) {
        loose(w);
        cmd->addWidget(w, w == ui_->MachineSlotsRunCommand ? 1 : 0);
    }
    cmd->addWidget(button(ui_->MachineSlotsRunCommandButtonRun));
    p->addLayout(cmd);
    p->addWidget(framed(ui_->MachineSlotsInfoFrame, {ui_->MachineSlotsInfoTitleLabel, ui_->MachineSlotsInfo}, 2), 1);
    ui_->MachineSlotsInfo->setMinimumHeight(150);
    auto* right = box(new QVBoxLayout, 0, 0, 0, 8, 0);
    right->addWidget(panel);

    auto* middle = box(new QHBoxLayout, 8, 8, 8, 0, 8);
    middle->addLayout(left, 1);
    middle->addLayout(right, 1);
    auto* root = box(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    loose(ui_->TopBar);
    ui_->TopBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    root->addWidget(ui_->TopBar);
    root->addLayout(middle, 1);
    setProperty("vclLaidOut", true);
    // as designed (1517 x 631, less the software lines' 31 when there are none), or bigger when the
    // texts need it
    layout()->activate();
    resize(QSize(1517, softwareLines_ ? 631 : 600).expandedTo(minimumSizeHint()));
}

// ButtonMachinesPanelShowHideClick: the window ends after the left side, or after the panel.
void CustomParametersDialog::setPanelVisible(bool on) {
    const bool was = ui_->PanelMachineSlotsGroupBox->isVisibleTo(this);
    if (was && !on) panelWidth_ = ui_->PanelMachineSlotsGroupBox->width() + 8;
    const int target = width() + (on ? panelWidth_ : -panelWidth_); // (reckoned before the window grows by itself)
    panel_ = on;
    ui_->PanelMachineSlotsGroupBox->setVisible(on);
    ui_->ButtonMachinesPanelShowHide->setText(on ? QString(QChar(0x25C0)) : QString(QChar(0x25B6))); // ◀ ▶
    QFont f = ui_->ButtonMachinesPanelShowHide->font();
    f.setFamily(QApplication::font().family());
    f.setPixelSize(10);
    ui_->ButtonMachinesPanelShowHide->setFont(f);
    layout()->activate();
    if (was != on) resize(std::max(minimumSizeHint().width(), target), height());
}

// ButtonReloadClick: CustomParam_ReadParams
void CustomParametersDialog::reload() {
    ui_->CustomParameters_Edit->setText(CustomParams::read(in_.file));
}

// ButtonOkClick: an empty box blinks instead of being saved (Delete removes a file).
void CustomParametersDialog::accept() {
    QString p = ui_->CustomParameters_Edit->text().trimmed();
    if (p.isEmpty()) {
        QLineEdit* e = ui_->CustomParameters_Edit;
        QString old = e->styleSheet();
        e->setStyleSheet("QLineEdit { background: #FFD8D8; }");
        QTimer::singleShot(400, e, [e, old] { e->setStyleSheet(old); });
        e->setFocus();
        return;
    }
    if (!CustomParams::write(in_.file, p)) {
        FileProblem::cantSave(this, windowTitle(), in_.file);
        return;
    }
    QDialog::accept();
}

void CustomParametersDialog::done(int r) {
    fe_.ini().writeInteger(kIniSection, kIniPanel, panel_ ? 1 : 0); // MachinesPanelReadWrite
    fe_.saveIni();
    QDialog::done(r);
}

// MachineCommandShowSlotsMediaText
void CustomParametersDialog::showSlotsMedia() {
    QString opt = ui_->MachinesSlotsRadioButton->isChecked() ? "-listslots" : "-listmedia";
    ui_->MachineSlotsRunCommand->setText(opt);
    runCommand(opt);
}

// MachineCommandExecute: "mame <machine> <options>", -listslots when no list option is given;
// the header's first line goes over the text, as a title.
QString CustomParametersDialog::selectedMachine() const {
    QModelIndex i = ui_->MachinesListView->currentIndex();
    return i.isValid() ? machines_->index(i.row(), 1).data().toString() : in_.game.name;
}

void CustomParametersDialog::runCommand(const QString& options) {
    QStringList args{selectedMachine()};
    QStringList opts = QProcess::splitCommand(options.trimmed().toLower());
    if (!opts.contains("-listslots") && !opts.contains("-listmedia")) opts << "-listslots";
    args << opts;
    ui_->MachineSlotsInfo->clear();
    ui_->MachineSlotsInfoTitleLabel->clear();
    QString exe = fe_.emulatorFile(sys_);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        ui_->MachineSlotsInfo->setPlainText(tr("MAME is not set up (Menu Options > MAME/Arcade > Emulators Setup)."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QProcess p;
    p.setWorkingDirectory(fe_.mameFolder(sys_));
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, args);
    bool ok = p.waitForFinished(20000);
    if (!ok) p.kill();
    QApplication::restoreOverrideCursor();
    QString out = QString::fromLocal8Bit(p.readAll());
    QStringList lines = out.split('\n');
    for (QString& l : lines) l.remove('\r');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    if (lines.size() >= 2 && lines[1].startsWith('-')) {
        ui_->MachineSlotsInfoTitleLabel->setText(lines[0]);
        lines = lines.mid(2);
    }
    if (!ok && lines.isEmpty()) lines << tr("MAME didn't answer.");
    ui_->MachineSlotsInfo->setPlainText(lines.join('\n'));
}

} // namespace eld
