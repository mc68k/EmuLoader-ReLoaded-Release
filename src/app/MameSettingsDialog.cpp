#include "MameSettingsDialog.h"
#include "FileProblem.h"
#include "ElWidgets.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "ui_FormMAMESettings.h"
#include "SettingsSearch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QTreeView>
#include <vector>

namespace eld {

// ------------------------------------------------------------- option table

namespace {
struct OptMap {
    std::vector<const char*> names; // first = name written to the ini; others are MAME's aliases
    const char* kind;               // bool, text, textnone, float, int, index, combo, choice, devmap
    const char* control;            // widget objectName in FormMAMESettings.ui
    const char* extra;              // decimals / index offset / value list / default / edit box
    bool mameIniOnly;               // only read from mame.ini (folders etc.)
};
const std::vector<OptMap> kOptions = {
#include "MameOptionMap.inc"
};
} // namespace

static QString findKey(const QMap<QString, QString>& v, const OptMap& o) {
    for (const char* n : o.names)
        if (v.contains(n)) return n;
    return {};
}

static QStringList choiceValues(const QString& spec, bool* ci) {
    // "auto=0|dsound=1|...;ci" -> list indexed by combo position
    QString s = spec;
    *ci = s.endsWith(";ci");
    if (*ci) s.chop(3);
    QStringList byIndex;
    for (const QString& p : s.split('|')) {
        QString v = p.section('=', 0, 0);
        int i = p.section('=', 1).toInt();
        while (byIndex.size() <= i) byIndex << QString();
        if (byIndex[i].isEmpty()) byIndex[i] = v; // first value listed for an index wins (as when writing)
    }
    return byIndex;
}

// ------------------------------------------------------------------ setup

MameSettingsDialog::MameSettingsDialog(Frontend& fe, SystemId sys, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormMAMESettings), fe_(fe), sys_(sys) {
    ui_->setupUi(this);
    vcl::apply(this);
#ifdef Q_OS_WIN
    sdlMame_ = QFileInfo(fe_.emulatorFile(sys_)).fileName().contains("sdl", Qt::CaseInsensitive);
#else
    sdlMame_ = true; // MAME is built with the SDL OSD everywhere except Windows
#endif
    {
        MameIniFile m; // EnableSettingsIniMAME: "sdlvideofps" is only in SDL MAME's ini
        if (m.load(fe_.mameIniPath(sys_)) && m.has("sdlvideofps")) sdlMame_ = true;
    }
    setWindowTitle(tr("Emulator Settings: %1").arg(QDir::toNativeSeparators(fe_.mameIniPath(sys_))));
    setupPages();
    setupExtras();
    setupButtons();
    if (sdlMame_ && ui_->Debugger->count() > 2) ui_->Debugger->setItemText(2, "QT");
    ui_->SaveValidateAllCustomFiles->setChecked(fe_.ini().readBool("Preferences", "MAMESettings_ValidateAllCustomFiles", false));
    for (const char* p : {"FolderROMs", "FolderIniFiles", "FolderArtworks"}) setupFolderList(p);

    // Value lists built at runtime in the Delphi code (PopulateVideoOutputMode, PopulateNumberProcessors).
    ui_->VideoOutputMode->clear();
    ui_->VideoOutputMode->addItems(sdlMame_ ? QStringList{"Auto", "Software", "OpenGL", "SDL2 Accel", "BGFX (cross-platform)", "None"}
                                            : QStringList{"Auto", "GDI", "DirectDraw", "Direct3D", "OpenGL", "BGFX (cross-platform)", "None"});
    ui_->NumberProcessors->clear();
    ui_->NumberProcessors->addItem("Auto");
    for (int i = 1; i <= std::max(1, QThread::idealThreadCount()); ++i) ui_->NumberProcessors->addItem(QString::number(i));

    ui_->LabelGameTitle->setText(tr("Emulator default settings (mame.ini)"));
    ui_->LabelEmulatorVersion->setText(fe_.emulatorVersion(sys_));
    ui_->LabelGameStatus->hide();
    ui_->SaveValidateAllCustomFiles->setEnabled(false);

    connect(ui_->ButtonOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui_->ButtonReadFile, &QPushButton::clicked, this, [this] { load(); });
    connect(ui_->ButtonResetToDefault, &QPushButton::clicked, this, [this] {
        // ButtonResetToDefaultClick: the emulator's own settings (mame.ini) only.
        MameIniFile mameIni;
        mameIni.load(fe_.mameIniPath(sys_));
        QMap<QString, QString> base;
        for (const QString& k : mameIni.keys()) base.insert(k.toLower(), mameIni.get(k));
        for (ScreenInfo& s : screens_) s = ScreenInfo{};
        apply(base, true);
    });
    load();
}

MameSettingsDialog::~MameSettingsDialog() = default;

void MameSettingsDialog::setupPages() {
    // The pages (side by side on the design canvas) one at a time, each in rows and columns made
    // from its design (vcl::autoLayout): as designed in English, and room for a longer text or a
    // larger font. The window is as big as its largest page.
    setStyleSheet({});
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0xf1, 0xf1, 0xf1)); // FormMAMESettings.Color := $00f1f1f1
    setPalette(pal);
    QWidget* panels[] = {ui_->Panel1_Folders, ui_->Panel2_Video1, ui_->Panel3_Video2, ui_->Panel4_Video3a,
                         ui_->Panel5_LUA, ui_->Panel6_Input, ui_->Panel7_Miscellaneous, ui_->Panel8_SDL};

    // Video 3: its BGFX settings, below the three sub page buttons, in a panel of their own, in
    // the same place as the Direct3D and OpenGL panels (one of the three shown)
    QWidget* v3 = ui_->Panel4_Video3a;
    const int header = 46;
    bgfx_ = new QWidget(v3);
    bgfx_->setObjectName("Panel4_Video3BGFX");
    bgfx_->setProperty("vclClass", "TPanel");
    const int v3w = v3->property("vclDesignCW").toInt(), v3h = v3->property("vclDesignCH").toInt();
    bgfx_->setProperty("vclDesignX", 0);
    bgfx_->setProperty("vclDesignY", header);
    bgfx_->setProperty("vclDesignW", v3w);
    bgfx_->setProperty("vclDesignH", v3h - header);
    bgfx_->setProperty("vclDesignCW", v3w);
    bgfx_->setProperty("vclDesignCH", v3h - header);
    QList<QWidget*> below; // (collected first: moving them changes the page's list of controls)
    for (QObject* o : v3->children())
        if (auto* w = qobject_cast<QWidget*>(o);
            w && w != bgfx_ && w->property("vclDesignY").isValid() && w->property("vclDesignY").toInt() >= header)
            below << w;
    for (QWidget* w : below)
        vcl::reparent(w, bgfx_, QPoint(w->property("vclDesignX").toInt(), w->property("vclDesignY").toInt() - header));
    vcl::reparent(ui_->Panel4_Video3b, v3, QPoint(0, header));
    vcl::reparent(ui_->Panel4_Video3c, v3, QPoint(0, header));
    // (moving controls in and out reset its design place: set last)
    bgfx_->setGeometry(0, header, v3w, v3h - header);
    for (auto [k, v] : {std::pair{"vclDesignX", 0}, {"vclDesignY", header}, {"vclDesignW", v3w}, {"vclDesignH", v3h - header},
                        {"vclDesignCW", v3w}, {"vclDesignCH", v3h - header}})
        bgfx_->setProperty(k, v);

    for (QWidget* p : panels) vcl::autoLayout(p);
    // OK and Cancel at the right edge when the window is wider than designed (a translation)
    for (QWidget* w : {static_cast<QWidget*>(ui_->ButtonOk), static_cast<QWidget*>(ui_->ButtonCancel)})
        w->setProperty("vclAnchors", "akTop,akRight");
    vcl::autoLayout(ui_->PanelBottom);
    showVideoEffects(2); // (the Direct3D page first, as in the original)

    // the window: the top bar, the page buttons (at the right), the pages, the buttons at the bottom
    setProperty("vclLaidOut", true); // (its layout places the bars, not their VCL alignment)
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(ui_->TopBar);
    ui_->TopBar->setFixedHeight(ui_->TopBar->property("vclDesignH").toInt());
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 10, 0);
    buttons->setSpacing(0);
    buttons->addStretch(1);
    const QList<QPushButton*> pageButtons = {ui_->ButtonPageFolders, ui_->ButtonPageVideo1, ui_->ButtonPageVideo2,
                                             ui_->ButtonPageVideo3, ui_->ButtonPageLUAandAudio, ui_->ButtonPageInput,
                                             ui_->ButtonPageMisc, ui_->ButtonPageSDLMAMEInput};
    for (QPushButton* b : pageButtons) {
        vcl::stopEliding(b);
        b->setMinimumSize(b->property("vclDesignW").toInt(), b->property("vclDesignH").toInt());
        buttons->addWidget(b);
    }
    lay->addLayout(buttons);
    pages_ = new QStackedLayout;
    for (QWidget* p : panels) pages_->addWidget(p);
    lay->addLayout(pages_);
    lay->addWidget(ui_->PanelBottom);
    lay->setSizeConstraint(QLayout::SetFixedSize);

    for (QPushButton* b : pageButtons)
        connect(b, &QPushButton::clicked, this, [this, b] { showPage(b->property("vclTag").toInt()); });
    ui_->ButtonPageSDLMAMEInput->setVisible(sdlMame_);
    for (QPushButton* b : {ui_->ButtonPageVideoEffectsBGFX, ui_->ButtonPageVideoEffectsHLSL, ui_->ButtonPageVideoEffectsGLSL})
        connect(b, &QPushButton::clicked, this, [this, b] { showVideoEffects(b->property("vclTag").toInt()); });
    ui_->PanelBottom->setProperty("vclTag", 1);
    showPage(1);
}

void MameSettingsDialog::showPage(int tag) {
    if (pages_ && tag >= 1 && tag <= pages_->count()) pages_->setCurrentIndex(tag - 1);
}

void MameSettingsDialog::showVideoEffects(int subTag) {
    bgfx_->setVisible(subTag == 1);
    ui_->Panel4_Video3b->setVisible(subTag == 2);
    ui_->Panel4_Video3c->setVisible(subTag == 3);
}

// ------------------------------------------------------- find a setting

// The page (and Video 3 sub page) a control is on: its page button, and the panel to show.
static void pageOf(Ui::FormMAMESettings* ui, QWidget* w, QPushButton** page, int* tag, QPushButton** sub, int* subTag) {
    QWidget* panels[] = {ui->Panel1_Folders, ui->Panel2_Video1, ui->Panel3_Video2, ui->Panel4_Video3a,
                         ui->Panel5_LUA, ui->Panel6_Input, ui->Panel7_Miscellaneous, ui->Panel8_SDL};
    QPushButton* buttons[] = {ui->ButtonPageFolders, ui->ButtonPageVideo1, ui->ButtonPageVideo2, ui->ButtonPageVideo3,
                              ui->ButtonPageLUAandAudio, ui->ButtonPageInput, ui->ButtonPageMisc, ui->ButtonPageSDLMAMEInput};
    *page = nullptr;
    *sub = nullptr;
    for (QWidget* p = w; p; p = p->parentWidget()) {
        if (p == ui->Panel4_Video3b) { *sub = ui->ButtonPageVideoEffectsHLSL; *subTag = 2; }
        if (p == ui->Panel4_Video3c) { *sub = ui->ButtonPageVideoEffectsGLSL; *subTag = 3; }
        for (int i = 0; i < 8; ++i)
            if (p == panels[i]) { *page = buttons[i]; *tag = i + 1; }
    }
    for (QWidget* p = w; p && !*sub; p = p->parentWidget())
        if (p->objectName() == "Panel4_Video3BGFX") { *sub = ui->ButtonPageVideoEffectsBGFX; *subTag = 1; }
}

void MameSettingsDialog::addSettingsTo(QList<SettingEntry>& out) {
    for (QWidget* w : {static_cast<QWidget*>(ui_->LabelGameTitle), static_cast<QWidget*>(ui_->LabelEmulatorVersion)})
        w->setProperty("searchSkip", true); // the game and MAME version, not settings
    SettingsSearch::addWindow(out, this, "mame", tr("MAME Settings"), [this](QWidget* w) {
        QPushButton *page, *sub;
        int tag = 0, subTag = 0;
        pageOf(ui_.get(), w, &page, &tag, &sub, &subTag);
        QStringList where;
        if (page) where << page->text().remove('&').simplified();
        if (sub) where << sub->text().remove('&').simplified();
        QString g = SettingsSearch::groupOf(w, this);
        if (!g.isEmpty()) where << g;
        return where;
    });
}

void MameSettingsDialog::reveal(const QString& objectName) {
    QWidget* w = findChild<QWidget*>(objectName);
    if (!w) return;
    QPushButton *page, *sub;
    int tag = 0, subTag = 0;
    pageOf(ui_.get(), w, &page, &tag, &sub, &subTag);
    if (page) {
        showPage(tag);
        page->setChecked(true);
    }
    if (sub) {
        showVideoEffects(subTag);
        sub->setChecked(true);
    }
    SettingsSearch::highlight(w);
}

// ---------------------------------------------------- folder list editors

QStandardItemModel* MameSettingsDialog::folderModel(const QString& prefix) const { return folders_.value(prefix); }

void MameSettingsDialog::setupFolderList(const QString& prefix) {
    auto* view = findChild<QTreeView*>(prefix);
    if (!view) return;
    auto* model = new QStandardItemModel(this);
    model->setHorizontalHeaderLabels({"Folder"});
    view->setModel(model);
    view->setRootIsDecorated(false);
    view->header()->setStretchLastSection(true);
    view->header()->hide();
    folders_.insert(prefix, model);
    auto btn = [&](const char* suffix) { return findChild<QPushButton*>(prefix + "Button" + suffix); };
    auto current = [view] { return view->currentIndex().row(); };
    auto pick = [this](const QString& start) {
        return QFileDialog::getExistingDirectory(this, tr("Select Folder"), resolvePath(fe_.mameFolder(sys_), start));
    };
    auto relative = [this](const QString& dir) {
        // Keep paths relative to the MAME folder when inside it (as EmuLoader does).
        QString rel = QDir(fe_.mameFolder(sys_)).relativeFilePath(dir);
        return rel.startsWith("..") ? QDir::toNativeSeparators(dir) : QDir::toNativeSeparators(rel);
    };
    if (auto* b = btn("Select")) // the "Add" button is named ...ButtonSelect in the Delphi form
        connect(b, &QPushButton::clicked, this, [=] {
            QString d = pick({});
            if (!d.isEmpty()) model->appendRow(new QStandardItem(relative(d)));
        });
    if (auto* b = btn("Edit"))
        connect(b, &QPushButton::clicked, this, [=] {
            int r = current();
            if (r < 0) return;
            QString d = pick(model->item(r)->text());
            if (!d.isEmpty()) model->item(r)->setText(relative(d));
        });
    if (auto* b = btn("Delete"))
        connect(b, &QPushButton::clicked, this, [=] { if (current() >= 0) model->removeRow(current()); });
    if (auto* b = btn("Clear")) connect(b, &QPushButton::clicked, this, [=] { model->removeRows(0, model->rowCount()); });
    auto move = [=](int delta) {
        int r = current(), t = r + delta;
        if (r < 0 || t < 0 || t >= model->rowCount()) return;
        model->insertRow(t, model->takeRow(r));
        view->setCurrentIndex(model->index(t, 0));
    };
    if (auto* b = btn("Up")) connect(b, &QPushButton::clicked, this, [=] { move(-1); });
    if (auto* b = btn("Down")) connect(b, &QPushButton::clicked, this, [=] { move(1); });
}

// --------------------------------------------------------- custom (chain)

void MameSettingsDialog::setCustom(const MameGame& game, int level) {
    game_ = game;
    level_ = level;
    machine_ = fe_.chainMachine(game);
    QString path = MameIniChain::path(level, machine_, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_));
    iniName_ = QFileInfo(path).completeBaseName(); // RAM Size: mame_ramsize.ini's section
    setWindowTitle(tr("Settings: %1").arg(QDir::toNativeSeparators(path)));
    ui_->LabelGameTitle->setText(game.title);
    QString info = "game: " + game.name;
    if (!game.cloneOf.isEmpty()) info += " [clone of " + game.cloneOf + "]";
    if (!game.bios.isEmpty()) info += " [bios: " + game.bios + "]";
    ui_->LabelEmulatorVersion->setText(info + "   -   editing " + QFileInfo(path).fileName());
    ui_->SaveValidateAllCustomFiles->setEnabled(true);
    // Folders apply to the emulator as a whole (EmuDefaultSettings in the Delphi code).
    ui_->Panel1_Folders->setEnabled(false);
    showPage(2);
    ui_->ButtonPageVideo1->setChecked(true);
    load();
}

// ------------------------------------------------------------------ read

void MameSettingsDialog::load() {
    MameIniFile mameIni;
    mameIni.load(fe_.mameIniPath(sys_));
    for (ScreenInfo& s : screens_) s = ScreenInfo{};
    enableForMameIni(mameIni);
    prepareExtras(mameIni);
    if (level_ == 0) loadUiIni();
    QMap<QString, QString> base;
    for (const QString& k : mameIni.keys()) base.insert(k.toLower(), mameIni.get(k));
    apply(base, true);
    populateShaderChains();
    if (level_ > 0) {
        // Then every chain file up to the one being edited (LoadCustomSettingsFiles).
        auto merged = MameIniChain::effective(machine_, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_), level_);
        apply(merged, false);
    }
}

void MameSettingsDialog::apply(const QMap<QString, QString>& v, bool fromMameIni) {
    for (const OptMap& o : kOptions) {
        if (o.mameIniOnly && !fromMameIni) continue;
        QString key = findKey(v, o);
        if (key.isEmpty()) continue;
        QString value = v.value(key);
        QWidget* w = findChild<QWidget*>(o.control);
        if (!w) continue;
        QString kind = o.kind, extra = o.extra;
        if (kind == "bool") {
            if (auto* c = qobject_cast<QCheckBox*>(w)) c->setChecked(value.trimmed().toInt() != 0);
        } else if (kind == "text" || kind == "textnone") {
            if (kind == "textnone" && value.compare("none", Qt::CaseInsensitive) == 0) value.clear();
            if (kind == "text" && value.isEmpty() && extra.startsWith("default=")) value = extra.mid(8);
            if (auto* e = qobject_cast<QLineEdit*>(w)) e->setText(value);
            else if (auto* cb = qobject_cast<QComboBox*>(w)) cb->setCurrentText(value);
        } else if (kind == "float" || kind == "int") {
            if (auto* s = qobject_cast<QSlider*>(w)) vcl::setSliderValue(s, value.toDouble());
            else if (auto* sp = qobject_cast<QSpinBox*>(w)) sp->setValue(value.toInt());
        } else if (kind == "index") {
            if (auto* cb = qobject_cast<QComboBox*>(w)) {
                int i = value.toInt() + QString(extra).toInt();
                cb->setCurrentIndex(i >= 0 && i < cb->count() ? i : 0);
            }
        } else if (kind == "combo") {
            if (auto* cb = qobject_cast<QComboBox*>(w)) {
                int i = cb->findText(value, Qt::MatchFixedString);
                cb->setCurrentIndex(i < 0 ? 0 : i);
            }
        } else if (kind == "ramsize") { // RAM Size: (Use Default), then the machine's choices by name
            if (auto* cb = qobject_cast<QComboBox*>(w)) {
                const int i = value.trimmed().isEmpty() ? 0 : cb->findText(value.trimmed(), Qt::MatchFixedString);
                cb->setCurrentIndex(i < 0 ? 0 : i);
            }
        } else if (kind == "choice") {
            if (auto* cb = qobject_cast<QComboBox*>(w)) {
                bool ci = false;
                QStringList vals = choiceValues(extra, &ci);
                int found = 0;
                for (const QString& p : QString(extra).remove(";ci").split('|'))
                    if (p.section('=', 0, 0).compare(value, ci ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0) {
                        found = p.section('=', 1).toInt();
                        break;
                    }
                cb->setCurrentIndex(found < cb->count() ? found : 0);
            }
        } else if (kind == "devmap") {
            auto* cb = qobject_cast<QComboBox*>(w);
            auto* e = findChild<QLineEdit*>(extra);
            if (cb && e) {
                bool isAuto = value.compare("auto", Qt::CaseInsensitive) == 0;
                cb->setCurrentIndex(isAuto ? 0 : 1);
                e->setEnabled(!isAuto);
                e->setText(isAuto ? QString() : value);
            }
        }
    }
    for (auto it = v.begin(); it != v.end(); ++it)
        if (!readSpecial(it.key(), it.value(), fromMameIni)) readExtra(it.key(), it.value(), fromMameIni);
    showScreen(ui_->ScreensSelector->currentIndex());
}

bool MameSettingsDialog::readSpecial(const QString& key, const QString& value, bool fromMameIni) {
    auto fill = [&](const char* prefix) {
        if (!fromMameIni) return;
        QStandardItemModel* m = folderModel(prefix);
        if (!m) return;
        m->removeRows(0, m->rowCount());
        for (const QString& p : splitPathList(value)) m->appendRow(new QStandardItem(p));
    };
    if (key == "rompath" || key == "rp") fill("FolderROMs");
    else if (key == "inipath") fill("FolderIniFiles");
    else if (key == "artpath") fill("FolderArtworks");
    else if (key == "samplerate" || key == "sr") {
        static const QStringList rates = {"11025", "22050", "44100", "48000", "96000", "192000"};
        int i = rates.indexOf(value.trimmed());
        ui_->SampleRate->setCurrentIndex(i < 0 ? 3 : i);
    } else if (key == "numprocessors" || key == "np") {
        int n = value.toInt();
        ui_->NumberProcessors->setCurrentIndex(value.compare("auto", Qt::CaseInsensitive) == 0 || n <= 0
                                                   ? 0 : std::min(n, ui_->NumberProcessors->count() - 1));
    } else if (key == "priority") {
        if (!value.isEmpty()) ui_->ThreadPriority->setCurrentIndex(value.toInt() + 15);
    } else if (key == "video") {
        static const QStringList win = {"auto", "gdi", "ddraw", "d3d", "opengl", "bgfx", "none"};
        static const QStringList sdl = {"auto", "soft", "opengl", "accel", "bgfx", "none"};
        int i = (sdlMame_ ? sdl : win).indexOf(value.toLower());
        ui_->VideoOutputMode->setCurrentIndex(i < 0 ? 0 : i);
    } else if (key == "autoboot_delay") {
        int d = value.toInt();
        if (d >= 0 && d < 181) vcl::setSliderValue(ui_->AutobootDelay, d);
    } else if (key == "rewind_capacity") {
        int c = value.isEmpty() ? 100 : std::clamp(value.toInt(), 1, 2048);
        if (auto* w = findChild<QWidget*>("SaveStateRewindCapacity")) {
            if (auto* e = qobject_cast<QLineEdit*>(w)) e->setText(QString::number(c));
            else vcl::setSliderValue(w, c);
        }
    } else return false;
    return true;
}

// ----------------------------------------------------------------- write

QMap<QString, QString> MameSettingsDialog::collect() const {
    QMap<QString, QString> out;
    for (const OptMap& o : kOptions) {
        QWidget* w = findChild<QWidget*>(o.control);
        if (!w || (o.mameIniOnly && level_ > 0)) continue;
        QString kind = o.kind, extra = o.extra, v;
        bool ok = true;
        if (kind == "bool") {
            auto* c = qobject_cast<QCheckBox*>(w);
            ok = c;
            if (c) v = c->isChecked() ? "1" : "0";
        } else if (kind == "text" || kind == "textnone") {
            if (auto* e = qobject_cast<QLineEdit*>(w)) v = e->text();
            else if (auto* cb = qobject_cast<QComboBox*>(w)) v = cb->currentText();
            else ok = false;
            if (kind == "textnone" && v.isEmpty()) v = "none";
        } else if (kind == "float") {
            v = QString::number(vcl::sliderValue(w), 'f', QString(extra).toInt());
        } else if (kind == "int") {
            if (auto* sp = qobject_cast<QSpinBox*>(w)) v = QString::number(sp->value());
            else v = QString::number(qRound(vcl::sliderValue(w)));
        } else if (kind == "index") {
            auto* cb = qobject_cast<QComboBox*>(w);
            ok = cb;
            if (cb) v = QString::number(std::max(0, cb->currentIndex()) - QString(extra).toInt());
        } else if (kind == "combo") {
            auto* cb = qobject_cast<QComboBox*>(w);
            ok = cb;
            if (cb) v = cb->currentText().toLower(); // LowerCase(<combo>.Text) in WriteMAMEIniFile
        } else if (kind == "ramsize") {
            // only when the machine has choices (mame_ramsize.ini); (Use Default) writes nothing after it
            auto* cb = qobject_cast<QComboBox*>(w);
            ok = cb && cb->isEnabled() && cb->count() > 1;
            if (ok) v = cb->currentIndex() <= 0 ? QString() : cb->currentText();
        } else if (kind == "choice") {
            auto* cb = qobject_cast<QComboBox*>(w);
            bool ci;
            QStringList vals = choiceValues(extra, &ci);
            ok = cb && cb->currentIndex() >= 0 && cb->currentIndex() < vals.size() && !vals[cb->currentIndex()].isEmpty();
            if (ok) v = vals[cb->currentIndex()];
        } else if (kind == "devmap") {
            auto* cb = qobject_cast<QComboBox*>(w);
            auto* e = findChild<QLineEdit*>(extra);
            ok = cb && e;
            if (ok) v = cb->currentIndex() <= 0 || e->text().isEmpty() ? "auto" : e->text();
        }
        if (ok)
            for (const char* n : o.names) out.insert(n, v); // the writer picks whichever name the file uses
    }
    writeSpecial(out);
    writeExtras(out);
    return out;
}

void MameSettingsDialog::writeSpecial(QMap<QString, QString>& out) const {
    auto join = [&](const char* prefix) {
        QStringList l;
        if (QStandardItemModel* m = folderModel(prefix))
            for (int r = 0; r < m->rowCount(); ++r) l << m->item(r)->text();
        return l.join(';');
    };
    if (level_ == 0) {
        out["rompath"] = join("FolderROMs");
        out["inipath"] = join("FolderIniFiles");
        out["artpath"] = join("FolderArtworks");
    }
    static const QStringList rates = {"11025", "22050", "44100", "48000", "96000", "192000"};
    out["samplerate"] = rates.value(ui_->SampleRate->currentIndex(), "48000");
    out["numprocessors"] = ui_->NumberProcessors->currentIndex() <= 0 ? "auto" : QString::number(ui_->NumberProcessors->currentIndex());
    out["priority"] = QString::number(ui_->ThreadPriority->currentIndex() - 15);
    static const QStringList win = {"auto", "gdi", "ddraw", "d3d", "opengl", "bgfx", "none"};
    static const QStringList sdl = {"auto", "soft", "opengl", "accel", "bgfx", "none"};
    out["video"] = (sdlMame_ ? sdl : win).value(ui_->VideoOutputMode->currentIndex(), "auto");
    out["autoboot_delay"] = QString::number(qRound(vcl::sliderValue(ui_->AutobootDelay)));
}

bool MameSettingsDialog::save(QString* error) {
    QMap<QString, QString> values = collect();
    MameIniFile mameIni;
    if (!mameIni.load(fe_.mameIniPath(sys_))) {
        if (error) *error = tr("Cannot read %1").arg(fe_.mameIniPath(sys_));
        return false;
    }
    if (level_ == 0) {
        // Rewrite values in place. Options this MAME version doesn't have stay out,
        // and options the dialog doesn't know are left untouched (WriteMAMEIniFile).
        // Values that are numerically unchanged keep their original text ("1.0" stays "1.0").
        for (const QString& k : mameIni.keys()) {
            if (!values.contains(k.toLower())) continue;
            QString nv = values.value(k.toLower()), ov = mameIni.get(k);
            bool a, b;
            double x = nv.toDouble(&a), y = ov.toDouble(&b);
            if (a && b && qFuzzyCompare(1 + x, 1 + y)) continue;
            mameIni.set(k, nv);
        }
        if (!mameIni.save(fe_.mameIniPath(sys_))) {
            if (error) *error = FileProblem::cantSaveText(fe_.mameIniPath(sys_));
            return false;
        }
        return saveUiIni(error) && savePluginIni(error);
    }
    // Custom file: only values that differ from what MAME would otherwise use.
    QString path = MameIniChain::path(level_, machine_, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_));
    QMap<QString, QString> baseline;
    if (ui_->SaveValidateAllCustomFiles->isChecked())
        baseline = MameIniChain::effective(machine_, fe_.mameIniPath(sys_), fe_.iniDir(sys_), fe_.mameBuild(sys_), level_ - 1);
    else
        for (const QString& k : mameIni.keys()) baseline.insert(k.toLower(), mameIni.get(k));
    auto same = [](const QString& a, const QString& b) {
        bool fa, fb;
        double x = a.toDouble(&fa), y = b.toDouble(&fb);
        return (fa && fb) ? qFuzzyCompare(1 + x, 1 + y) : a == b;
    };
    MameIniFile old;
    old.load(path);
    MameIniFile out;
    for (const QString& k : mameIni.keys()) {
        QString lk = k.toLower();
        if (values.contains(lk)) {
            if (!same(values.value(lk), baseline.value(lk, mameIni.get(k)))) out.set(k, values.value(lk));
        } else if (old.has(k)) {
            out.set(k, old.get(k)); // keep options this dialog doesn't edit
        }
    }
    QFile::remove(path);
    if (out.empty()) return true;
    if (!out.save(path)) {
        if (error) *error = FileProblem::cantSaveText(path);
        return false;
    }
    return true;
}

void MameSettingsDialog::accept() {
    QString err;
    if (!save(&err)) {
        QMessageBox::critical(this, tr("Emulator Settings"), err);
        return;
    }
    QDialog::accept();
}

} // namespace eld
