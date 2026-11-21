#include "EmulatorsSetupDialog.h"

#include "SettingsLayout.h"
#include "VclRuntime.h"
#include "ui_FormArcadeEmulatorsSetup.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <tuple>

namespace eld {

namespace {
// The links under the emulator file (Get MAME: / Get HBMAME:). Sites that have moved
// point to where they are now.
struct Link { const char* label; const char* url; };
const Link kLinks[] = {
    {"LabelMAMELink1", "https://www.mamedev.org"},
    {"LabelMAMELink2", "https://hbmame.1emulation.com"},
    {"LabelMAMELink3", "https://github.com/mahlemiut/wolfmame"},
    {"LabelMAMELink4", "https://forums.bannister.org/ubbthreads.php?ubb=postlist&Board=8"},
};
} // namespace

EmulatorsSetupDialog::EmulatorsSetupDialog(Frontend& fe, SystemId current, Hooks hooks, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeEmulatorsSetup), fe_(fe), hooks_(std::move(hooks)) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Arcade Emulators Setup"));

    // SystemSelector: an icon per system (ELV_PopulateSystems), in an icon view.
    systems_ = new QListWidget(this);
    systems_->setObjectName("EmulatorSystems");
    systems_->setViewMode(QListView::IconMode);
    systems_->setIconSize(QSize(48, 48));
    systems_->setGridSize(QSize(96, 82));
    systems_->setMovement(QListView::Static);
    systems_->setFlow(QListView::LeftToRight);
    systems_->setFrameShape(QFrame::NoFrame);
    systems_->setFont(QFont("Verdana", 8));
    systems_->setStyleSheet("QListWidget { background: transparent; }");
    const QString icons = fe_.resourcesFolder() + "main_icons/";
    int start = 0;
    for (SystemId id : {SystemId::MAME, SystemId::HBMAME}) {
        auto* it = new QListWidgetItem(QIcon(fe_.mainIcon(id == SystemId::MAME ? "emu_mame.ico" : "emu_hbmame.ico")),
                                       Frontend::section(id), systems_);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        if (id == current) start = ids_.size();
        ids_ << id;
        pending_[int(id)] = saved_[int(id)] = fe_.emulator(id);
    }

    // The emulator: typing a path counts too (Arcade_execChange); the version is recomputed on Ok.
    connect(ui_->Arcade_exec, &QLineEdit::textEdited, this, [this](const QString& t) {
        pending_[int(this->current())].file = t.trimmed();
    });
    connect(ui_->Arcade_versioninfo, &QLineEdit::textEdited, this, [this](const QString& t) {
        pending_[int(this->current())].version = t;
    });
    ui_->ButtonBrowseArcade_exec->setText(tr("Select"));
    ui_->ButtonBrowseArcade_exec->setToolTip(tr("Click here to select a file"));
    ui_->ButtonClearArcade_exec->setText(tr("Clear"));
    ui_->ButtonClearArcade_exec->setToolTip(tr("Click here to clear Emulator File and Version Info"));
    connect(ui_->ButtonBrowseArcade_exec, &QPushButton::clicked, this, &EmulatorsSetupDialog::browse);
    connect(ui_->ButtonClearArcade_exec, &QPushButton::clicked, this, &EmulatorsSetupDialog::clearSystem);
    connect(ui_->ButtonUpdateSystem, &QPushButton::clicked, this, &EmulatorsSetupDialog::resetSystem);
    connect(ui_->ButtonSetOptions, &QPushButton::clicked, this, &EmulatorsSetupDialog::defaultSettings);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ButtonUpdateSystem->setToolTip(tr("Detect the emulator's version and file date again"));

    // Links (LabelMAMELink1Click): the original keeps the address in the hint.
    for (const Link& l : kLinks)
        if (auto* lab = findChild<QLabel*>(l.label)) {
            lab->setText(QString("<a href=\"%1\" style=\"color:#000080; text-decoration:none\">%2</a>")
                             .arg(QString::fromLatin1(l.url).toHtmlEscaped(), lab->text().toHtmlEscaped()));
            lab->setTextFormat(Qt::RichText);
            lab->setOpenExternalLinks(true);
            lab->setToolTip(QString::fromLatin1(l.url));
            lab->adjustSize();
        }

    connect(systems_, &QListWidget::currentRowChanged, this, &EmulatorsSetupDialog::showSystem);
    systems_->setCurrentRow(start);
    showSystem(start);
}

EmulatorsSetupDialog::~EmulatorsSetupDialog() = default;

SystemId EmulatorsSetupDialog::current() const {
    return ids_.value(std::max(0, shown_), SystemId::MAME);
}

// SetSystemInfo
void EmulatorsSetupDialog::showSystem(int row) {
    if (row < 0 || row >= ids_.size()) return;
    shown_ = row;
    SystemId id = ids_[row];
    const EmulatorInfo& e = pending_[int(id)];
    ui_->Arcade_exec->setText(QDir::toNativeSeparators(e.file));
    ui_->Arcade_exec->setCursorPosition(0);
    ui_->Arcade_versioninfo->setText(e.version);
    // the file and its settings named after the system chosen (MAME File, HBMAME Settings ...)
    const QString name = Frontend::section(id), ini = name.toLower() + ".ini";
    ui_->ButtonSetOptions->setToolTip(tr("%1's own settings (%2)").arg(name, ini));
    if (emulatorBox_) {
        emulatorBox_->setTitle(Frontend::description(id, e.file));
        links_->setVisible(id == SystemId::MAME);
        hbLinks_->setVisible(id == SystemId::HBMAME);
        ui_->LabelArcade_exec->setText(tr("%1 File").arg(name));
        ui_->ButtonSetOptions->setText(tr("%1 Settings...").arg(name));
        explain_->setText(tr("Detect the Version Again reads the version and the file's date from %1 again. "
                             "%1 Settings opens its own settings (%2).").arg(name, ini));
    }
}

// The Settings window's Emulators tab: the emulator of the system chosen with the switch. (The
// original's AlterMAME 1 and 2, older MAME builds to run a game with when MAME failed it, are gone:
// today's ROM sets no longer match an old build's.)
void EmulatorsSetupDialog::settingsLayout() {
    using settingsui::Group;
    QLabel* like = new QLabel(this);
    like->setStyleSheet("color: #6E6E6E;");
    like->hide();
    auto hint = [like](const QString& t) { return settingsui::hint(t, like); };

    // Get MAME: / Get HBMAME: the system's links, with dots between (the one of the system chosen shows)
    auto linkRow = [](const QString& title, const QString& name, const QList<QLabel*>& labels) {
        auto* row = new QWidget;
        row->setObjectName(name);
        auto* lh = new QHBoxLayout(row);
        lh->setContentsMargins(0, 0, 0, 0);
        lh->setSpacing(6);
        lh->addWidget(new QLabel(title));
        bool first = true;
        for (QLabel* l : labels) {
            if (!l) continue;
            if (!first) lh->addWidget(new QLabel(QStringLiteral("•")));
            first = false;
            lh->addWidget(settingsui::take(l));
            l->show();
        }
        lh->addStretch(1);
        return row;
    };
    ui_->LabelMAMELink4->setText(ui_->LabelMAMELink4->text().replace(">SDL<", ">" + tr("SDL MAME (Linux and macOS)") + "<"));
    links_ = linkRow(tr("Get MAME:"), "MameLinks", {ui_->LabelMAMELink1, ui_->LabelMAMELink3, ui_->LabelMAMELink4});
    hbLinks_ = linkRow(tr("Get HBMAME:"), "HbmameLinks", {ui_->LabelMAMELink2});

    ui_->LabelArcade_versioninfo->setText(tr("Version"));
    ui_->ButtonUpdateSystem->setText(tr("Detect the Version Again"));
    explain_ = hint(QString());
    Group emu(tr("Emulator"));
    emulatorBox_ = emu.box();
    emu.row(new QLabel(tr("System")), settingsui::systemSwitch(systems_))
        .row(ui_->LabelArcade_exec, ui_->Arcade_exec, {ui_->ButtonBrowseArcade_exec, ui_->ButtonClearArcade_exec})
        .row(ui_->LabelArcade_versioninfo, ui_->Arcade_versioninfo)
        .row(nullptr, links_)
        .row(nullptr, hbLinks_)
        .space(4)
        .line({ui_->ButtonUpdateSystem, ui_->ButtonSetOptions})
        .text(explain_);

    // the same weight for every label and check box (the original's are bold here and there)
    for (QWidget* w : std::initializer_list<QWidget*>{ui_->LabelArcade_exec, ui_->LabelArcade_versioninfo}) {
        QFont f = w->font();
        f.setBold(false);
        w->setFont(f);
    }
    settingsui::Page p("EmulatorsPage", tr("The emulators that run the games: MAME, and HBMAME for homebrew and hacked games."), 1);
    p.add(0, emu).finish();
    settingsui::setPage(this, p.widget());
    showSystem(shown_);
}

EmulatorInfo EmulatorsSetupDialog::detect(SystemId s, const QString& exe, const std::function<void(QProcess*)>& prepare) {
    QProcess p;
    p.setWorkingDirectory(QFileInfo(exe).absolutePath());
    if (prepare) prepare(&p);
    p.start(exe, {"-version"});
    QString answer;
    if (p.waitForStarted(5000)) {
        p.waitForFinished(15000);
        answer = QString::fromUtf8(p.readAllStandardOutput());
    }
    return Frontend::describe(s, exe, answer);
}

void EmulatorsSetupDialog::ensureIniFiles(Frontend& fe, SystemId s, const std::function<void(QProcess*)>& prepare) {
    QString exe = fe.emulatorFile(s);
    if (exe.isEmpty() || !QFileInfo::exists(exe)) return;
    QString folder = QFileInfo(exe).absolutePath();
    bool missing = !QFileInfo::exists(fe.mameIniPath(s));
    // ui.ini appeared in MAME 0.172
    int build = fe.mameBuild(s);
    if (!missing && (build >= 172 || build < 0) && !QFileInfo::exists(QDir(folder).filePath("ui.ini"))) missing = true;
    if (!missing) return;
    QProcess c;
    c.setWorkingDirectory(folder);
    if (prepare) prepare(&c);
    c.start(exe, {"-createconfig"});
    c.waitForFinished(30000);
}

// ButtonBrowseArcade_execClick
void EmulatorsSetupDialog::browse() {
    SystemId id = current();
    QString start = pending_[int(id)].file.isEmpty() ? QString() : QFileInfo(pending_[int(id)].file).absolutePath();
    QString exe = QFileDialog::getOpenFileName(this, tr("Select an emulator file for %1").arg(Frontend::description(id, {})),
                                               start,
#ifdef Q_OS_WIN
                                               tr("Programs (*.exe)")
#else
                                               {}
#endif
    );
    if (exe.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    EmulatorInfo e = detect(id, exe, hooks_.prepare);
    QApplication::restoreOverrideCursor();
    if (e.build.isEmpty())
        QMessageBox::warning(this, windowTitle(),
                             tr("%1 didn't answer \"-version\", so it may not be MAME. It is kept; check the file.")
                                 .arg(QFileInfo(exe).fileName()));
    pending_[int(id)] = e;
    showSystem(shown_);
}

// ButtonClearArcade_execClick
void EmulatorsSetupDialog::clearSystem() {
    pending_[int(current())] = EmulatorInfo{};
    showSystem(shown_);
}

// ButtonUpdateSystemClick: the version and the file date again.
void EmulatorsSetupDialog::resetSystem() {
    SystemId id = current();
    QString exe = pending_[int(id)].file;
    if (exe.isEmpty() || !QFileInfo::exists(exe)) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pending_[int(id)] = detect(id, exe, hooks_.prepare);
    QApplication::restoreOverrideCursor();
    showSystem(shown_);
}

// ButtonSetOptionsClick: the emulator's default settings (mame.ini). The settings window reads
// the emulator from the setup, so this system's file is put in place first.
void EmulatorsSetupDialog::defaultSettings() {
    SystemId id = current();
    const EmulatorInfo& e = pending_[int(id)];
    if (e.file.isEmpty() || !QFileInfo::exists(e.file)) {
        ui_->Arcade_exec->setFocus(); // BlinkBkEdit
        ui_->Arcade_exec->setStyleSheet("QLineEdit { background: #ffd0d0; }");
        QTimer::singleShot(600, ui_->Arcade_exec, [this] { ui_->Arcade_exec->setStyleSheet({}); });
        return;
    }
    fe_.setEmulator(id, e);
    applied_ = true;
    ensureIniFiles(fe_, id, hooks_.prepare);
    if (hooks_.openSettings) hooks_.openSettings(id);
}

// ButtonOkClick + VerifyEmulator
void EmulatorsSetupDialog::accept() {
    bool any = false;
    for (SystemId id : ids_) any |= !pending_[int(id)].file.isEmpty();
    if (!any) {
        QMessageBox::critical(this, windowTitle(),
                              tr("No emulator files were selected. Please select at least one emulator, "
                                 "or use Cancel."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (SystemId id : ids_) {
        EmulatorInfo e = pending_[int(id)];
        if (e.file.isEmpty() || !QFileInfo::exists(e.file)) {
            fe_.setEmulator(id, EmulatorInfo{}); // a missing file is not kept
            continue;
        }
        e = detect(id, e.file, hooks_.prepare);
        fe_.setEmulator(id, e);
        ensureIniFiles(fe_, id, hooks_.prepare);
    }
    QApplication::restoreOverrideCursor();
    fe_.saveIni();
    QDialog::accept();
}

void EmulatorsSetupDialog::reject() {
    if (applied_) // Default Settings put a file in place: back to what was set up before
        for (SystemId id : ids_) fe_.setEmulator(id, saved_[int(id)]);
    QDialog::reject();
}

} // namespace eld
