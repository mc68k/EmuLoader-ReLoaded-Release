#include "FileVersionsDialog.h"

#include "VclRuntime.h"
#include "ui_FormArcadeFileVersionsLarge.h"

#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QShortcut>
#include <QVBoxLayout>

namespace eld {

QString FileVersionsDialog::gamesListVersion(const QString& elFile) {
    QFile f(elFile);
    if (!f.open(QIODevice::ReadOnly)) return {};
    bool info = false;
    for (int i = 0; i < 20 && !f.atEnd(); ++i) { // the header is at the top
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith('[')) {
            if (info) break;
            info = line.compare("[info_file]", Qt::CaseInsensitive) == 0;
        } else if (info && line.startsWith("version=", Qt::CaseInsensitive)) {
            return line.mid(8);
        }
    }
    return {};
}

FileVersionsDialog::FileVersionsDialog(const Frontend& fe, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeFileVersionsLarge) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Arcade Files Versions"));
    QWidget* panels[] = {ui_->Panel1, ui_->Panel2}; // MAME, HBMAME
    for (QWidget* p : panels) p->hide();
    // "MAME v0.289 (Aug 03 2026)" -> "v0.289 (Aug 03 2026)", as AddSystems trims it
    auto trim = [](QString v) {
        int i = v.indexOf(" v");
        return i >= 0 ? v.mid(i + 1) : v;
    };
    const QString icons = fe.resourcesFolder() + "main_icons/";
    int n = 0;
    for (SystemId id : {SystemId::MAME, SystemId::HBMAME}) {
        const EmulatorInfo e = fe.emulator(id);
        if (!e.isSet()) continue; // ValidateArcadeEmulatorFile
        QWidget* panel = panels[n];
        const QString idx = QString::number(n + 1);
        if (auto* img = panel->findChild<QLabel*>("Image" + idx)) {
            img->setPixmap(QIcon(fe.mainIcon(id == SystemId::MAME ? "emu_mame.ico" : "emu_hbmame.ico")).pixmap(128, 128));
            img->setScaledContents(true);
        }
        if (auto* l = panel->findChild<QLabel*>("LabelSystemTitle" + idx)) l->setText(Frontend::description(id, e.file));
        if (auto* l = panel->findChild<QLabel*>("LabelEmuFileName" + idx)) {
            // (a long path breaks after its folders' separators too, not only at spaces and dashes)
            QString shown = QDir::toNativeSeparators(e.file);
            shown.replace(QDir::separator(), QString(QDir::separator()) + QChar(0x200B));
            l->setText(shown);
            l->setWordWrap(true);
            l->setToolTip(QDir::toNativeSeparators(e.file));
        }
        if (auto* l = panel->findChild<QLabel*>("LabelVersionInfo" + idx)) {
            const QString emu = trim(e.version), games = trim(gamesListVersion(fe.databaseFile(id)));
            QString gl = games.isEmpty() ? tr("(none)") : games;
            // New: a games list made with another MAME says so
            bool differs = !games.isEmpty() && emu.section(' ', 0, 0) != games.section(' ', 0, 0);
            l->setTextFormat(Qt::RichText);
            l->setText(QString("<table cellspacing=0 cellpadding=0><tr><td>%1:&nbsp;</td><td>%2</td></tr>"
                                   "<tr><td>%3:&nbsp;</td><td>%4</td></tr></table>")
                           .arg(tr("Emulator").toHtmlEscaped(), emu.toHtmlEscaped(), tr("Games List").toHtmlEscaped(),
                                differs ? QString("<span style=\"color:#c00000\">%1</span>").arg(gl.toHtmlEscaped()) : gl.toHtmlEscaped()));
            if (differs)
                l->setToolTip(tr("The games list was made with another version of the emulator: create it again "
                                 "(Menu Options > MAME/Arcade > Create Arcade Games List)."));
        }
        panel->show();
        ++n;
    }
    shown_ = n;
    // the systems shown one under another, each as big as its texts need
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->setSizeConstraint(QLayout::SetFixedSize);
    for (QWidget* p : panels) {
        vcl::autoLayout(p);
        // the path: wrapped at the design's width (data, not text that a language makes longer)
        if (auto* path = p->findChild<QLabel*>(p == ui_->Panel1 ? "LabelEmuFileName1" : "LabelEmuFileName2"))
            path->setMinimumWidth(0), path->setMaximumWidth(350);
        lay->addWidget(p);
    }
    setProperty("vclLaidOut", true);
    // FormKeyPress: Enter or Esc closes it
    auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(enter, &QShortcut::activated, this, &QDialog::accept);
}

FileVersionsDialog::~FileVersionsDialog() = default;

} // namespace eld
