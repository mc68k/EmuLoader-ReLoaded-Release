// MameSettingsExtras - the MAME options whose handling in EmuLoader's uMAMESettings.pas
// isn't a plain "option <-> control" mapping (the "MANUAL" list of
// tools/extract_mame_options.py):
//
//   screen/aspect/resolution/view (+0..3)  per-screen settings behind the "Screens" selector
//   bios                                   BIOS sets list (arcade\games\mame_biossets.ini)
//   language                               languages found in languagepath
//   plugin (+ plugin.ini)                  LUA plugins found in pluginspath
//   ctrlrpath / ctrlr                      controller definition files found in ctrlrpath
//   snapsize                               "auto" or <width>x<height>
//   debugger_font / debugger_font_size     font picker
//   audio_latency                          frames (decimal) from MAME 0.278, 1..5 before
//   gl_lib                                 SDL: auto or a custom library
//   skip_warnings                          lives in ui.ini, next to mame.ini
#include "MameSettingsDialog.h"
#include "FileProblem.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormMAMESettings.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFontDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QFileDialog>
#include <QSlider>
#include <QStandardItemModel>
#include <QTreeView>

namespace eld {

namespace {
struct EnableRow {
    std::vector<const char*> options;  // any of these in mame.ini...
    std::vector<const char*> controls; // ...enables these (they start disabled in the form)
};
const std::vector<EnableRow> kEnable = {
#include "MameOptionEnable.inc"
};

const char* kScreenSuffix[5] = {"", "0", "1", "2", "3"};

QStandardItemModel* makeList(QTreeView* view, const QStringList& columns, QObject* parent) {
    auto* m = new QStandardItemModel(parent);
    m->setHorizontalHeaderLabels(columns);
    view->setModel(m);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->header()->setStretchLastSection(true);
    if (columns.size() == 1) view->header()->hide();
    return m;
}

// "auto" in the list is shown as "Auto"; everything else as written.
int findItem(QComboBox* cb, const QString& value) {
    int i = cb->findText(value, Qt::MatchFixedString); // case-insensitive, like TStrings.IndexOf
    return i < 0 ? 0 : i;
}

QString comboValue(const QComboBox* cb) {
    QString t = cb->currentText();
    return t.compare("auto", Qt::CaseInsensitive) == 0 || t.isEmpty() ? QString("auto") : t;
}

QStringList folders(const QString& pathList, const QString& base) {
    QStringList out;
    for (const QString& p : splitPathList(pathList)) {
        QString d = resolvePath(base, p);
        if (QFileInfo(d).isDir()) out << d;
    }
    return out;
}

// GenerateControllerDefinitionsFilesList (uCommon.pas): *.cfg and *.zip names, and folders
// holding a default.ini, anywhere below the ctrlr folders.
QStringList controllerDefinitions(const QStringList& roots) {
    QStringList out;
    for (const QString& r : roots) {
        QDirIterator it(r, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFileInfo f(it.next());
            QString name;
            if (f.suffix().compare("cfg", Qt::CaseInsensitive) == 0 || f.suffix().compare("zip", Qt::CaseInsensitive) == 0)
                name = f.completeBaseName();
            else if (f.fileName().compare("default.ini", Qt::CaseInsensitive) == 0)
                name = f.dir().dirName();
            if (!name.isEmpty() && !out.contains(name, Qt::CaseInsensitive)) out << name;
        }
    }
    out.removeIf([](const QString& s) { return s.compare("standard", Qt::CaseInsensitive) == 0; });
    return out;
}

// Click on a label (the debugger font sample is a TShadowLabel with OnClick).
class ClickFilter : public QObject {
public:
    ClickFilter(QObject* parent, std::function<void()> f) : QObject(parent), f_(std::move(f)) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
            f_();
            return true;
        }
        return false;
    }

private:
    std::function<void()> f_;
};
} // namespace

// ------------------------------------------------------------------ setup

void MameSettingsDialog::setupExtras() {
    // ---- screens (ScreensSelectorSelect / Screen*Select)
    connect(ui_->ScreensSelector, &QComboBox::currentIndexChanged, this, [this](int i) { showScreen(i); });
    auto store = [this](QComboBox* cb, QString ScreenInfo::*field) {
        connect(cb, &QComboBox::currentIndexChanged, this, [this, cb, field] {
            if (!loadingScreen_) screens_[screen_].*field = comboValue(cb);
        });
    };
    store(ui_->ScreenName, &ScreenInfo::name);
    store(ui_->ScreenAspectRatio, &ScreenInfo::aspect);
    store(ui_->ScreenResolution, &ScreenInfo::resolution);
    store(ui_->ScreenRefreshRate, &ScreenInfo::refresh);
    store(ui_->ScreenView, &ScreenInfo::view);
    connect(ui_->ScreenButtonDefaultSettings, &QPushButton::clicked, this, [this] {
        screens_[screen_] = ScreenInfo{};
        showScreen(ui_->ScreensSelector->currentIndex());
    });

    // ---- lists
    biosModel_ = makeList(ui_->BiosSetsListView, {"BIOS", "Set"}, this);
    langModel_ = makeList(ui_->DisplayLanguageList, {"Language"}, this);
    pluginModel_ = makeList(ui_->LUAPluginsToEnable, {"Plugin", "Name", "Version", "Author"}, this);
    ui_->BiosSetsListView->header()->resizeSection(0, ui_->BiosSetsListView->width() * 2 / 3);
    QHeaderView* ph = ui_->LUAPluginsToEnable->header();
    int pw = ui_->LUAPluginsToEnable->width();
    ph->resizeSection(0, pw * 45 / 100);
    ph->resizeSection(1, pw * 18 / 100);
    ph->resizeSection(2, pw * 12 / 100);

    // ---- snapsize (SnapSizeAutoClick)
    auto snapAuto = [this] {
        bool a = ui_->SnapSizeAuto->isChecked();
        ui_->SnapSizeWidth->setEnabled(!a);
        ui_->SnapSizeHeight->setEnabled(!a);
        ui_->SnapSizeCustomXLabel->setEnabled(!a);
    };
    connect(ui_->SnapSizeAuto, &QCheckBox::toggled, this, snapAuto);
    snapAuto();

    // ---- debugger font (DebuggerFontClick / DebuggerFontButtonResetClick / DebuggerFontSizeChange)
    auto showFont = [this] {
        int size = qRound(vcl::sliderValue(ui_->DebuggerFontSize));
        ui_->DebuggerFontSizeLabel->setText(size > 0 ? tr("Font Size %1").arg(size) : tr("Font Size [Auto]"));
        QFont f(debuggerFont_);
        f.setPointSize(size > 0 ? size : 9);
        ui_->DebuggerFont->setFont(f);
        ui_->DebuggerFont->setText(tr("Font"));
        ui_->DebuggerFont->setToolTip(debuggerFont_ + (debuggerFont_ == "Lucida Console" ? "\n[Auto]" : ""));
    };
    connect(ui_->DebuggerFontSize, &QSlider::valueChanged, this, showFont);
    ui_->DebuggerFont->setCursor(Qt::PointingHandCursor);
    ui_->DebuggerFont->installEventFilter(new ClickFilter(this, [this, showFont] {
        bool ok = false;
        QFont cur(debuggerFont_);
        int size = qRound(vcl::sliderValue(ui_->DebuggerFontSize));
        cur.setPointSize(size > 0 ? size : 9);
        QFont f = QFontDialog::getFont(&ok, cur, this, tr("Debugger Font"));
        if (!ok) return;
        debuggerFont_ = f.family();
        vcl::setSliderValue(ui_->DebuggerFontSize, f.pointSize() == 9 ? 0 : f.pointSize());
        showFont();
    }));
    connect(ui_->DebuggerFontButtonReset, &QPushButton::clicked, this, [this, showFont] {
        debuggerFont_ = "Lucida Console"; // "debugger_font" default, mame\uimetrics.cpp
        vcl::setSliderValue(ui_->DebuggerFontSize, 0);
        showFont();
    });
    showFont();

    // ---- audio latency (ConfigureAudioLatency): MAME 0.278 changed it from levels 1..5 to frames.
    int build = fe_.mameBuild(sys_);
    decimalLatency_ = build >= 278 || build < 0;
    ui_->AudioLatency->clear();
    if (decimalLatency_) {
        ui_->AudioLatency->setEditable(true);
        ui_->AudioLatency->addItems({"0.0", "0.5", "1.0", "1.5", "2.0", "5.0", "50.0"});
        ui_->AudioLatencyLabel->setText(tr("Latency (frames)"));
        ui_->AudioLatency->setToolTip(tr("Audio latency: 0.0 to 50.0 frames (1 frame = 20 ms). 0 uses the sound module "
                                      "default. Use a decimal point; increase to reduce glitches, decrease for "
                                      "responsiveness. Unsupported by PipeWire, PulseAudio and SDL."));
        ui_->AudioLatencyButtonReset->setToolTip(tr("Use the sound module default (0.0)"));
    } else {
        ui_->AudioLatency->setEditable(false);
        for (int i = 1; i <= 5; ++i) ui_->AudioLatency->addItem(QString("%1/5").arg(i));
        ui_->AudioLatencyLabel->setText(tr("Audio Latency"));
        ui_->AudioLatencyButtonReset->setToolTip(tr("Set default latency"));
    }
    connect(ui_->AudioLatencyButtonReset, &QPushButton::clicked, this, [this] {
        if (decimalLatency_) ui_->AudioLatency->setCurrentText("0.0");
        else ui_->AudioLatency->setCurrentIndex(1); // MAME's default level 2
    });
    ui_->AudioLatencyButtonReset->click();

    // ---- gl_lib (SDLAlternativeLibGLToUseSelect)
    auto glLib = [this] {
        bool custom = ui_->SDLAlternativeLibGLToUse->currentIndex() == 1;
        ui_->SDLAlternativeLibGLToUseCustom->setEnabled(custom);
        ui_->SDLAlternativeLibGLToUseCustomButtonSelect->setEnabled(custom);
    };
    connect(ui_->SDLAlternativeLibGLToUse, &QComboBox::currentIndexChanged, this, glLib);
    glLib();
}

QString MameSettingsDialog::pluginIniPath() const {
    // TFormMain.GetMAMEPluginFile: next to MAME, else in its ini folder.
    QDir emu(fe_.mameFolder(sys_));
    if (QFileInfo::exists(emu.filePath("plugin.ini"))) return emu.filePath("plugin.ini");
    QString inIni = QDir(fe_.iniDir(sys_)).filePath("plugin.ini");
    return QFileInfo::exists(inIni) ? inIni : emu.filePath("plugin.ini");
}

void MameSettingsDialog::enableForMameIni(const MameIniFile& mameIni) {
    // EnableSettingsIniMAME: what this MAME build doesn't have stays grayed out.
    for (const EnableRow& r : kEnable) {
        bool has = false;
        for (const char* o : r.options) has = has || mameIni.has(o);
        if (!has) continue;
        for (const char* c : r.controls)
            if (QWidget* w = findChild<QWidget*>(c)) w->setEnabled(true);
    }
    if (mameIni.has("ramsize") || mameIni.has("ram")) // RAMSizeDisablePanel(True)
        for (QWidget* w : {static_cast<QWidget*>(ui_->RAMSizeGroupBox), static_cast<QWidget*>(ui_->RAMSizeLabel),
                           static_cast<QWidget*>(ui_->RAMSize)})
            w->setEnabled(true);
    { // PopulateRAMSize: no RAM size in mame.ini; a machine's choices come from mame_ramsize.ini
        // (written with the games list), "(Use Default)" first
        const QString file = fe_.gamesFolder() + Frontend::fileBase(sys_) + "_ramsize.ini";
        const ElDatabase::RamSizes ram = level_ == 0 ? ElDatabase::RamSizes{} : ElDatabase::loadRamSizes(file, iniName_);
        ui_->RAMSize->clear();
        ui_->RAMSize->addItem(tr("(Use Default)"));
        QString title = tr("RAM Size");
        if (ram.options.isEmpty()) {
            ui_->RAMSizeLabel->setEnabled(false);
            ui_->RAMSize->setEnabled(false);
        } else {
            for (const auto& o : ram.options) {
                ui_->RAMSize->addItem(o.first);
                bool num = false;
                const qlonglong bytes = o.second.toLongLong(&num);
                ui_->RAMSize->setItemData(ui_->RAMSize->count() - 1, num ? tr("%1 bytes").arg(QLocale().toString(bytes)) : o.second,
                                          Qt::ToolTipRole);
            }
            if (!ram.defaultName.isEmpty()) {
                title = tr("RAM Size (Default: %1)").arg(ram.defaultName);
                ui_->RAMSize->setToolTip(tr("MAME's default for %1: %2").arg(iniName_, ram.defaultName));
            }
        }
        if (auto* l = qobject_cast<QLabel*>(ui_->RAMSizeGroupBoxLabel)) l->setText(title);
    }
    if (mameIni.has("beam")) { // MAME before 0.171: a single beam width
        ui_->VectorBeamWidthMinLabel->setText(tr("Beam Width [1.00]"));
        ui_->VectorBeamWidthMinLabel->setToolTip(tr("Beam Width [%2.2f]"));
        ui_->VectorBeamWidthMin->setToolTip(tr("Set vector beam width"));
        for (QWidget* w : {static_cast<QWidget*>(ui_->VectorBeamWidthMaxLabel), static_cast<QWidget*>(ui_->VectorBeamWidthMax),
                           static_cast<QWidget*>(ui_->VectorBeamIntensityWeightLabel),
                           static_cast<QWidget*>(ui_->VectorBeamIntensityWeight)})
            w->setEnabled(false);
    }
    ui_->UIModeKeyButtonDetectKey->setEnabled(ui_->UIModeKeyCustom->isEnabled());
}

void MameSettingsDialog::prepareExtras(const MameIniFile& mameIni) {
    QString base = fe_.mameFolder(sys_);

    // Controller definitions (ctrlrpath) for the "ctrlr" list.
    ui_->ControllerDefinitions->clear();
    ui_->ControllerDefinitions->addItem("(None)");
    QStringList ctrl = controllerDefinitions(folders(mameIni.get("ctrlrpath"), base));
    ctrl.sort(Qt::CaseInsensitive);
    ui_->ControllerDefinitions->addItems(ctrl);

    // Languages (PopulateDisplayLanguage): one folder per language; "Chinese_Simplified" is
    // shown as "Chinese (Simplified)", which is also what MAME accepts.
    langModel_->removeRows(0, langModel_->rowCount());
    for (const QString& dir : folders(mameIni.get("languagepath"), base)) {
        for (const QString& d : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
            QString title = d;
            if (title.contains('_')) title = title.replace(title.indexOf('_'), 1, " (") + ")";
            if (!langModel_->findItems(title).isEmpty()) continue;
            auto* it = new QStandardItem(title);
            if (d.compare("english", Qt::CaseInsensitive) == 0) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
                langModel_->insertRow(0, it);
            } else {
                langModel_->appendRow(it);
            }
        }
    }

    // LUA plugins (PopulateLUAPlugins_ListView): <pluginspath>\<name>\plugin.json, "type": "plugin".
    pluginModel_->removeRows(0, pluginModel_->rowCount());
    for (const QString& dir : folders(mameIni.get("pluginspath"), base)) {
        for (const QString& d : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile f(QDir(dir).filePath(d + "/plugin.json"));
            if (!f.open(QFile::ReadOnly)) continue;
            QJsonObject o = QJsonDocument::fromJson(f.readAll()).object().value("plugin").toObject();
            if (o.isEmpty() || o.value("type").toString().compare("plugin", Qt::CaseInsensitive) != 0) continue;
            QString name = o.value("name").toString();
            if (name.isEmpty()) continue;
            auto* title = new QStandardItem(o.value("description").toString());
            title->setCheckable(true);
            pluginModel_->appendRow({title, new QStandardItem(name), new QStandardItem(o.value("version").toString()),
                                     new QStandardItem(o.value("author").toString())});
        }
    }
    pluginModel_->sort(0, Qt::AscendingOrder);
    if (level_ == 0) { // plugin.ini holds the enabled plugins for the emulator (ReadPluginIni)
        MameIniFile pi;
        if (pi.load(pluginIniPath()))
            for (int r = 0; r < pluginModel_->rowCount(); ++r)
                pluginModel_->item(r, 0)->setCheckState(pi.get(pluginModel_->item(r, 1)->text()).trimmed() == "1"
                                                            ? Qt::Checked : Qt::Unchecked);
    }

    // BIOS sets (LoadBiosList): only for a machine that runs on a BIOS, from mame_biossets.ini.
    biosModel_->removeRows(0, biosModel_->rowCount());
    biosModel_->appendRow({new QStandardItem(tr("[Use Default]")), new QStandardItem(QString())});
    if (level_ > 0 && !game_.bios.isEmpty()) {
        WinIni bs;
        if (bs.load(fe_.biosSetsFile(sys_)) && bs.sectionExists(game_.bios)) {
            ui_->BiosSetsGroupBoxLabel->setText(tr("Bios: %1").arg(bs.readString("Descriptions", game_.bios)));
            QString def = bs.readString(game_.bios, "default_bios");
            QList<QList<QStandardItem*>> rows;
            for (const QString& k : bs.keys(game_.bios)) {
                if (k == "default_bios" || k == "selected_bios") continue;
                auto* t = new QStandardItem(bs.readString(game_.bios, k));
                if (k == def) {
                    QFont f = t->font();
                    f.setBold(true);
                    t->setFont(f);
                }
                rows.push_back({t, new QStandardItem(k)});
            }
            std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
                return QString::localeAwareCompare(a[0]->text(), b[0]->text()) < 0;
            });
            for (auto& r : rows) biosModel_->appendRow(r);
        }
    }
    ui_->BiosSetsListView->setCurrentIndex(biosModel_->index(0, 0));

    // skip_warnings is in ui.ini; it's a setting for MAME as a whole (Disable_MAMEui_Settings).
    ui_->SkipWarnings->setEnabled(level_ == 0 && QFileInfo::exists(QFileInfo(fe_.mameIniPath(sys_)).dir().filePath("ui.ini")));
}

// ------------------------------------------------------------------ screens

void MameSettingsDialog::showScreen(int index) {
    // ScreensSelectorSelect: item 0 is "All Screens" (screen, aspect, ...), then Screen 1..4.
    screen_ = std::clamp(index, 0, 4);
    const ScreenInfo& s = screens_[screen_];
    loadingScreen_ = true;
    ui_->ScreenName->setCurrentIndex(findItem(ui_->ScreenName, s.name));
    ui_->ScreenAspectRatio->setCurrentIndex(findItem(ui_->ScreenAspectRatio, s.aspect));
    ui_->ScreenResolution->setCurrentIndex(findItem(ui_->ScreenResolution, s.resolution));
    ui_->ScreenRefreshRate->setCurrentIndex(findItem(ui_->ScreenRefreshRate, s.refresh));
    ui_->ScreenView->setCurrentIndex(findItem(ui_->ScreenView, s.view));
    loadingScreen_ = false;
    // Values the lists don't have (a resolution typed into the ini, say) are kept as they are.
}

// ------------------------------------------------------------------ read

bool MameSettingsDialog::readExtra(const QString& key, const QString& value, bool fromMameIni) {
    QString v = value.trimmed();
    for (int i = 0; i < 5; ++i) {
        QString sfx = kScreenSuffix[i];
        if (key == "screen" + sfx) { screens_[i].name = v.isEmpty() ? "auto" : v; return true; }
        if (key == "aspect" + sfx || (i == 0 && key == "screen_aspect")) { screens_[i].aspect = v.isEmpty() ? "auto" : v; return true; }
        if (key == "resolution" + sfx || key == "r" + sfx) {
            // SetResolutionOption: "<w>x<h>[@<refresh>]", "auto"; 0x0 and 0 mean auto.
            QString res = v.section('@', 0, 0), rate = v.contains('@') ? v.section('@', 1) : QString();
            if (v.compare("auto", Qt::CaseInsensitive) == 0) res = rate = "auto";
            screens_[i].resolution = res.isEmpty() || res == "0x0" ? "auto" : res;
            screens_[i].refresh = rate.isEmpty() || rate == "0" ? "auto" : rate;
            return true;
        }
        if (key == "view" + sfx) {
            // FixScreenViewNameRead: "Screen 0 Cropped" -> "Cropped"
            QString l = v.toLower();
            screens_[i].view = l.contains("cropped") ? "Cropped" : l.contains("stretched") ? "Stretched" : (v.isEmpty() ? "auto" : v);
            return true;
        }
    }
    if (key == "bios") {
        for (int r = 0; r < biosModel_->rowCount(); ++r)
            if (biosModel_->item(r, 1)->text() == v) {
                ui_->BiosSetsListView->setCurrentIndex(biosModel_->index(r, 0));
                break;
            }
    } else if (key == "language" || key == "lang") {
        languageAtLoad_ = v;
        QString l = v;
        if (l.startsWith('"') && l.endsWith('"') && l.size() >= 2) l = l.mid(1, l.size() - 2);
        int row = 0;
        if (!l.isEmpty() && l.compare("English", Qt::CaseInsensitive) != 0)
            for (int r = 0; r < langModel_->rowCount(); ++r)
                if (langModel_->item(r)->text().compare(l, Qt::CaseInsensitive) == 0) { row = r; break; }
        if (langModel_->rowCount()) ui_->DisplayLanguageList->setCurrentIndex(langModel_->index(row, 0));
    } else if (key == "plugin") {
        if (level_ > 0) { // custom files: the plugins to enable, comma separated
            QStringList on = v.split(',', Qt::SkipEmptyParts);
            for (QString& s : on) s = s.trimmed();
            for (int r = 0; r < pluginModel_->rowCount(); ++r)
                pluginModel_->item(r, 0)->setCheckState(on.contains(pluginModel_->item(r, 1)->text(), Qt::CaseInsensitive)
                                                            ? Qt::Checked : Qt::Unchecked);
        }
    } else if (key == "ctrlrpath" || key == "ctrlr_directory") {
        if (fromMameIni) ui_->FolderControllerDefinitions->setText(value);
    } else if (key == "snapsize") {
        QString s = v.toLower();
        bool a = s == "auto" || !s.contains('x');
        ui_->SnapSizeAuto->setChecked(a);
        ui_->SnapSizeWidth->setText(a ? QString() : s.section('x', 0, 0));
        ui_->SnapSizeHeight->setText(a ? QString() : s.section('x', 1));
    } else if (key == "debugger_font" || key == "dfont") {
        debuggerFont_ = v.isEmpty() || v == "auto" ? QString("Lucida Console") : v;
        emit ui_->DebuggerFontSize->valueChanged(ui_->DebuggerFontSize->value()); // refresh the sample
    } else if (key == "debugger_font_size" || key == "dfontsize") {
        vcl::setSliderValue(ui_->DebuggerFontSize, v.toDouble());
    } else if (key == "audio_latency" || (decimalLatency_ && key == "alat")) {
        if (decimalLatency_) {
            bool ok = false;
            double d = v.toDouble(&ok);
            if (ok && d >= 0 && d <= 50) ui_->AudioLatency->setCurrentText(v); // keep the text as written
        } else {
            int i = v.toInt() - 1;
            if (i >= 0 && i < ui_->AudioLatency->count()) ui_->AudioLatency->setCurrentIndex(i);
        }
    } else if (key == "debugger") {
        // Index 2 is "Windows" (QT for SDL MAME); a value the list doesn't know (a debugger
        // added in a later MAME) is added to the list so it isn't lost on save.
        QString l = v.toLower();
        int i = l == "auto" ? 0 : l == "internal" ? 1 : (l == "windows" || l == "qt") ? 2 : l == "none" ? 3 : -1;
        if (i < 0 && !v.isEmpty()) {
            i = ui_->Debugger->findText(v, Qt::MatchFixedString);
            if (i < 0) { ui_->Debugger->addItem(v); i = ui_->Debugger->count() - 1; }
        }
        ui_->Debugger->setCurrentIndex(std::max(i, 0));
    } else if (key == "bgfx_screen_chains") {
        if (fromMameIni || level_ > 0) shaderChainsAtLoad_ = v.isEmpty() ? "default" : v;
        return false; // the text box itself is filled by the option table
    } else if (key == "gl_lib") {
        bool custom = !v.isEmpty() && v != "auto";
        ui_->SDLAlternativeLibGLToUse->setCurrentIndex(custom ? 1 : 0);
        ui_->SDLAlternativeLibGLToUseCustom->setText(custom ? v : QString());
    } else {
        return false;
    }
    return true;
}

void MameSettingsDialog::loadUiIni() {
    MameIniFile ui;
    if (ui.load(QFileInfo(fe_.mameIniPath(sys_)).dir().filePath("ui.ini")) && ui.has("skip_warnings"))
        ui_->SkipWarnings->setChecked(ui.get("skip_warnings").trimmed().toInt() != 0);
}

// ----------------------------------------------------------------- write

void MameSettingsDialog::writeExtras(QMap<QString, QString>& out) const {
    for (int i = 0; i < 5; ++i) {
        QString sfx = kScreenSuffix[i];
        const ScreenInfo& s = screens_[i];
        out["screen" + sfx] = s.name;
        out["aspect" + sfx] = s.aspect;
        // SetScreenResolution: the refresh rate only goes with a resolution, or as 0x0@<rate>.
        QString r = s.resolution;
        if (r != "auto") {
            if (!s.refresh.isEmpty() && s.refresh != "auto") r += "@" + s.refresh;
        } else if (s.refresh != "auto") {
            r = "0x0@" + s.refresh;
        }
        out["resolution" + sfx] = r;
        // FixScreenViewNameWrite: "Cropped" -> "Screen <n> Cropped"
        QString n = i == 0 ? "0" : QString::number(i - 1), l = s.view.toLower();
        out["view" + sfx] = l.contains("cropped") ? "Screen " + n + " Cropped"
                          : l.contains("stretched") ? "Screen " + n + " Stretched" : s.view;
    }
    if (level_ > 0) {
        QModelIndex b = ui_->BiosSetsListView->currentIndex();
        out["bios"] = b.isValid() ? biosModel_->item(b.row(), 1)->text() : QString();
        QStringList on;
        for (int r = 0; r < pluginModel_->rowCount(); ++r)
            if (pluginModel_->item(r, 0)->checkState() == Qt::Checked) on << pluginModel_->item(r, 1)->text();
        if (pluginModel_->rowCount()) out["plugin"] = on.join(',');
    }
    if (langModel_->rowCount()) {
        QModelIndex l = ui_->DisplayLanguageList->currentIndex();
        QString lang = l.isValid() ? langModel_->item(l.row())->text() : QString();
        // English (or nothing chosen) keeps an empty value empty: both mean MAME's built-in English.
        bool english = lang.isEmpty() || lang.compare("English", Qt::CaseInsensitive) == 0;
        out["language"] = english && languageAtLoad_.isEmpty() ? QString() : english ? QString("English") : lang;
    }
    if (level_ == 0) out["ctrlrpath"] = ui_->FolderControllerDefinitions->text();
    out["ctrlr"] = ui_->ControllerDefinitions->currentIndex() <= 0 ? QString() : ui_->ControllerDefinitions->currentText();
    QString snap = "auto";
    if (!ui_->SnapSizeAuto->isChecked() && !ui_->SnapSizeWidth->text().isEmpty() && !ui_->SnapSizeHeight->text().isEmpty())
        snap = ui_->SnapSizeWidth->text() + "x" + ui_->SnapSizeHeight->text();
    out["snapsize"] = snap;
    out["debugger_font"] = debuggerFont_ == "Lucida Console" ? QString("auto") : debuggerFont_;
    out["debugger_font_size"] = QString::number(qRound(vcl::sliderValue(ui_->DebuggerFontSize)));
    out["audio_latency"] = decimalLatency_ ? ui_->AudioLatency->currentText().trimmed()
                                           : QString::number(ui_->AudioLatency->currentIndex() + 1);
    int dbg = ui_->Debugger->currentIndex();
    out["debugger"] = dbg == 0 ? QString("auto") : dbg == 1 ? QString("internal") : dbg == 2 ? QString(sdlMame_ ? "qt" : "windows")
                    : dbg == 3 ? QString("none") : ui_->Debugger->currentText();
    out["gl_lib"] = ui_->SDLAlternativeLibGLToUse->currentIndex() == 1 && !ui_->SDLAlternativeLibGLToUseCustom->text().isEmpty()
                        ? ui_->SDLAlternativeLibGLToUseCustom->text() : QString("auto");
}

bool MameSettingsDialog::saveUiIni(QString* error) const {
    if (level_ != 0 || !ui_->SkipWarnings->isEnabled()) return true;
    QString path = QFileInfo(fe_.mameIniPath(sys_)).dir().filePath("ui.ini");
    MameIniFile ui;
    if (!ui.load(path) || !ui.has("skip_warnings")) return true;
    QString v = ui_->SkipWarnings->isChecked() ? "1" : "0";
    if (ui.get("skip_warnings").trimmed() == v) return true;
    ui.set("skip_warnings", v);
    if (ui.save(path)) return true;
    if (error) *error = FileProblem::cantSaveText(path);
    return false;
}

bool MameSettingsDialog::savePluginIni(QString* error) const {
    // WritePluginIni: only when editing the emulator's own settings.
    if (level_ != 0 || !ui_->LUAPluginsToEnable->isEnabled() || pluginModel_->rowCount() == 0) return true;
    QString path = pluginIniPath();
    QString text = "#\r\n# PLUGINS OPTIONS\r\n#\r\n";
    for (int r = 0; r < pluginModel_->rowCount(); ++r)
        text += QString("%1 %2\r\n").arg(pluginModel_->item(r, 1)->text(), -25)
                    .arg(pluginModel_->item(r, 0)->checkState() == Qt::Checked ? 1 : 0);
    if (writeTextFile(path, text, false)) return true;
    if (error) *error = FileProblem::cantSaveText(path);
    return false;
}

} // namespace eld

namespace eld {

// ------------------------------------------------------------------ buttons

QString MameSettingsDialog::relativeToMame(const QString& path) const {
    // Paths inside the MAME folder are kept relative to it, as in the folder lists.
    QString rel = QDir(fe_.mameFolder(sys_)).relativeFilePath(path);
    return QDir::toNativeSeparators(rel.startsWith("..") || QDir::isAbsolutePath(rel) ? path : rel);
}

QStringList MameSettingsDialog::artworkFolders() const {
    QStringList out;
    if (QStandardItemModel* m = folderModel("FolderArtworks"))
        for (int r = 0; r < m->rowCount(); ++r) {
            QString d = resolvePath(fe_.mameFolder(sys_), m->item(r)->text());
            if (QFileInfo(d).isDir()) out << d;
        }
    return out;
}

namespace {
QString quoted(const QString& s) { return s.contains(' ') && !s.startsWith('"') ? '"' + s + '"' : s; }

// Qt key -> MAME key name (input code names, as uimodekey expects them).
QString mameKeyName(QKeyEvent* k) {
    int key = k->key();
    bool right = k->nativeScanCode() == 0x36 || k->nativeScanCode() == 0x11d || k->nativeScanCode() == 0x138;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return QString(QChar('A' + key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return (k->modifiers() & Qt::KeypadModifier) ? QString("%1_PAD").arg(key - Qt::Key_0) : QString::number(key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return QString("F%1").arg(key - Qt::Key_F1 + 1);
    switch (key) {
    case Qt::Key_ScrollLock: return "SCRLOCK";
    case Qt::Key_Tab: return "TAB";
    case Qt::Key_Escape: return "ESC";
    case Qt::Key_Return: return "ENTER";
    case Qt::Key_Enter: return "ENTER_PAD";
    case Qt::Key_Space: return "SPACE";
    case Qt::Key_Backspace: return "BACKSPACE";
    case Qt::Key_Insert: return "INSERT";
    case Qt::Key_Delete: return "DEL";
    case Qt::Key_Home: return "HOME";
    case Qt::Key_End: return "END";
    case Qt::Key_PageUp: return "PGUP";
    case Qt::Key_PageDown: return "PGDN";
    case Qt::Key_Up: return "UP";
    case Qt::Key_Down: return "DOWN";
    case Qt::Key_Left: return "LEFT";
    case Qt::Key_Right: return "RIGHT";
    case Qt::Key_Shift: return right ? "RSHIFT" : "LSHIFT";
    case Qt::Key_Control: return right ? "RCONTROL" : "LCONTROL";
    case Qt::Key_Alt: return "LALT";
    case Qt::Key_AltGr: return "RALT";
    case Qt::Key_Meta: return "LWIN";
    case Qt::Key_Menu: return "MENU";
    case Qt::Key_CapsLock: return "CAPSLOCK";
    case Qt::Key_NumLock: return "NUMLOCK";
    case Qt::Key_Pause: return "PAUSE";
    case Qt::Key_Print: return "PRTSCR";
    case Qt::Key_Minus: return (k->modifiers() & Qt::KeypadModifier) ? "MINUS_PAD" : "MINUS";
    case Qt::Key_Plus: return "PLUS_PAD";
    case Qt::Key_Asterisk: return "ASTERISK";
    case Qt::Key_Slash: return (k->modifiers() & Qt::KeypadModifier) ? "SLASH_PAD" : "SLASH";
    case Qt::Key_Equal: return "EQUALS";
    case Qt::Key_BracketLeft: return "OPENBRACE";
    case Qt::Key_BracketRight: return "CLOSEBRACE";
    case Qt::Key_Semicolon: return "COLON";
    case Qt::Key_Apostrophe: return "QUOTE";
    case Qt::Key_Backslash: return "BACKSLASH";
    case Qt::Key_Comma: return "COMMA";
    case Qt::Key_Period: return (k->modifiers() & Qt::KeypadModifier) ? "DEL_PAD" : "STOP";
    case Qt::Key_QuoteLeft: return "TILDE";
    default: return {};
    }
}

class KeyCatcher : public QObject {
public:
    KeyCatcher(QLineEdit* e) : QObject(e), e_(e) {}
    bool armed = false;
    bool eventFilter(QObject*, QEvent* ev) override {
        if (!armed || ev->type() != QEvent::KeyPress) {
            if (armed && ev->type() == QEvent::FocusOut) disarm();
            return false;
        }
        QString name = mameKeyName(static_cast<QKeyEvent*>(ev));
        if (!name.isEmpty()) e_->setText(name);
        disarm();
        return true;
    }
    void arm() {
        armed = true;
        e_->setStyleSheet("QLineEdit { background: #fff3b0; }"); // SetEditBkColor
        e_->setPlaceholderText(QCoreApplication::translate("eld::MameSettingsDialog", "Press a key..."));
        e_->setFocus();
    }
    void disarm() {
        armed = false;
        e_->setStyleSheet({});
        e_->setPlaceholderText({});
    }

private:
    QLineEdit* e_;
};
} // namespace

void MameSettingsDialog::populateShaderChains() {
    // PopulateBGFXShaderChains_ListView: <bgfx_path>\chains\*.json, with "name" and "author".
    chainsModel_->removeRows(0, chainsModel_->rowCount());
    QString path = ui_->BGFXPath->text().isEmpty() ? QString("bgfx") : ui_->BGFXPath->text();
    QDir chains(resolvePath(fe_.mameFolder(sys_), path) + "/chains");
    for (const QFileInfo& f : chains.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile file(f.filePath());
        QString title, author;
        if (file.open(QFile::ReadOnly)) {
            QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
            title = o.value("name").toString();
            author = o.value("author").toString();
        }
        chainsModel_->appendRow({new QStandardItem(title.isEmpty() ? f.completeBaseName() : title),
                                 new QStandardItem(f.completeBaseName()), new QStandardItem(author)});
    }
    chainsModel_->sort(0);
}

void MameSettingsDialog::setupButtons() {
    auto click = [this](QPushButton* b, std::function<void()> f) { connect(b, &QPushButton::clicked, this, std::move(f)); };
    QString mame = fe_.mameFolder(sys_);
    auto startDir = [mame](const QString& current) {
        QString first = splitPathList(current).value(0);
        QString d = first.isEmpty() ? QString() : resolvePath(mame, first);
        if (!d.isEmpty() && QFileInfo(d).isFile()) d = QFileInfo(d).absolutePath();
        return !d.isEmpty() && QFileInfo(d).isDir() ? d : mame;
    };

    // Folders (DialogSelectFolder): some options take a list (separated by ';'), the new folder is added.
    const std::pair<const char*, bool> folderEdits[] = {
        {"FolderSamples", true}, {"FolderGameSnapshots", true}, {"FolderCheatFiles", true}, {"FolderCrosshairFiles", true},
        {"FolderFontFiles", true}, {"FolderControllerDefinitions", true}, {"FolderGamesConfigurations", false},
        {"FolderMemoryCards", false}, {"FolderInputDeviceLogs", false}, {"FolderSaveStates", false}, {"FolderDiff", false},
        {"FolderLanguageFiles", false}, {"FolderPluginFiles", false}, {"FolderNVRAM", false}, {"FolderHashFiles", false},
        {"HLSLPath", false}, {"FolderDebuggerComments", false}, {"BGFXPath", false}, {"FolderManualsPDF", false}};
    for (const auto& folder : folderEdits) {
        const char* name = folder.first;
        const bool multi = folder.second;
        auto* e = findChild<QLineEdit*>(name);
        auto* b = findChild<QPushButton*>(QString(name) + "ButtonSelect");
        if (!e || !b) continue;
        click(b, [=] {
            QString title = e->toolTip().isEmpty() ? tr("Select Folder") : e->toolTip().section('\n', 0, 0);
            QString d = QFileDialog::getExistingDirectory(this, title, startDir(e->text()));
            if (d.isEmpty()) return;
            d = relativeToMame(d);
            e->setText(multi && !e->text().isEmpty() ? e->text() + ";" + d : d);
        });
    }

    // Files (DialogOpenFile)
    auto pickFile = [this, startDir](QLineEdit* e, const QString& title, const QString& filter) {
        QString f = QFileDialog::getOpenFileName(this, title, startDir(e->text()), filter);
        return f.isEmpty() ? f : relativeToMame(f);
    };
    click(ui_->DebuggerScriptButtonSelect, [=] {
        QString f = pickFile(ui_->DebuggerScript, tr("Select a debugger script file"), tr("All files (*)"));
        if (!f.isEmpty()) ui_->DebuggerScript->setText(f);
    });
    click(ui_->AutobootLuaScriptButtonSelect, [=] {
        QString f = pickFile(ui_->AutobootLuaScript, tr("Select a LUA script file"), tr("LUA scripts (*.lua);;All files (*)"));
        if (!f.isEmpty()) ui_->AutobootLuaScript->setText(f);
    });
    click(ui_->SDLAlternativeLibGLToUseCustomButtonSelect, [=] {
        QString f = pickFile(ui_->SDLAlternativeLibGLToUseCustom, tr("Select an alternative libGL.so file to use"),
                             tr("Libraries (*.so *.so.* *.dylib *.dll);;All files (*)"));
        if (!f.isEmpty()) ui_->SDLAlternativeLibGLToUseCustom->setText(f);
    });
    click(ui_->SDLKeymapFilenameButtonSelect, [=] {
        QString f = pickFile(ui_->SDLKeymapFilename, tr("Select a keymap file"), tr("Keymap files (*.dat *.txt *.map);;All files (*)"));
        if (!f.isEmpty()) ui_->SDLKeymapFilename->setText(f);
    });
    // LUT textures (SelectLUTTextureFile)
    for (QLineEdit* e : {ui_->BGFXLUTTextureName, ui_->BloomLUTTextureScreen, ui_->BloomLUTTextureUI}) {
        auto* b = findChild<QPushButton*>(e->objectName() + "ButtonSelect");
        if (b) click(b, [=] {
            QString f = pickFile(e, tr("Select a LUT texture file"), tr("PNG images (*.png);;All files (*)"));
            if (!f.isEmpty()) e->setText(f);
        });
    }
    // GLSL shaders (SelectGLSLShaderMAME): the path without the file extension.
    for (int i = 0; i < 10; ++i)
        for (const char* kind : {"MAME", "Screen"}) {
            auto* e = findChild<QLineEdit*>(QString("GLSL_Shader_%1%2").arg(kind).arg(i));
            auto* b = findChild<QPushButton*>(QString("GLSL_Shader_%1%2ButtonSelect").arg(kind).arg(i));
            if (!e || !b) continue;
            bool isMame = QString(kind) == "MAME";
            click(b, [=] {
                QString f = pickFile(e, isMame ? tr("Select a GLSL shader MAME file") : tr("Select a GLSL shader screen file"),
                                     tr("GLSL shaders (*.vsh *.fsh);;All files (*)"));
                if (f.isEmpty()) return;
                QFileInfo fi(f);
                e->setText(QDir::toNativeSeparators(QDir(fi.path()).filePath(fi.completeBaseName())));
            });
        }
    // Files from the artwork folders (SelectFileName 28, 31, 32)
    auto fromArtwork = [this](QLineEdit* e, const QString& title, const QString& filter, bool baseName) {
        QStringList dirs = artworkFolders();
        if (dirs.isEmpty()) return;
        QString f = QFileDialog::getOpenFileName(this, title, dirs.first(), filter);
        if (f.isEmpty()) return;
        e->setText(quoted(baseName ? QFileInfo(f).completeBaseName() : QFileInfo(f).fileName()));
    };
    click(ui_->EffectOverlayButtonSelect, [=] {
        fromArtwork(ui_->EffectOverlay, tr("Select an effect overlay"), tr("PNG images (*.png);;All files (*)"), true);
    });
    click(ui_->ShadowMaskTextureButtonSelect, [=] {
        fromArtwork(ui_->ShadowMaskTexture, tr("Select a shadow mask texture"), tr("PNG images (*.png);;All files (*)"), false);
    });
    click(ui_->BGFXShadowMaskTextureNameButtonSelect, [=] {
        fromArtwork(ui_->BGFXShadowMaskTextureName, tr("Select a shadow mask texture"), tr("PNG images (*.png);;All files (*)"), false);
    });
    // UI font (SelectFileName 29): a font from the font folder, "ui" means MAME's own font.
    click(ui_->UIFontButtonSelect, [=] {
        QString dir = startDir(ui_->FolderFontFiles->text());
        QString f = QFileDialog::getOpenFileName(this, tr("Select a font"), dir, tr("Fonts (*.bdf *.ttf *.otf);;All files (*)"));
        if (f.isEmpty()) return;
        QString name = QFileInfo(f).completeBaseName();
        ui_->UIFont->setText(quoted(name.compare("ui", Qt::CaseInsensitive) == 0 ? QString("default") : name));
    });

    // Defaults (the *ButtonReset / *ButtonDefault handlers)
    click(ui_->HTTPServerButtonReset, [this] { ui_->HTTPPort->setText("8080"); ui_->HTTPPath->setText("web"); });
    click(ui_->SnapNameButtonDefault, [this] { ui_->SnapName->setText("%g/%i"); });
    click(ui_->SnapViewButtonDefault, [this] { ui_->SnapView->setCurrentIndex(0); });
    click(ui_->BGFXShadowMaskTextureNameButtonReset, [this] { ui_->BGFXShadowMaskTextureName->setText("slot-mask.png"); });
    click(ui_->BGFXLUTTextureNameButtonReset, [this] { ui_->BGFXLUTTextureName->clear(); });
    click(ui_->UIModeKeyCustomButtonReset, [this] { ui_->UIModeKeyCustom->setText("SCRLOCK"); });
    // The communication defaults are the edit boxes' hints in the form.
    click(ui_->CommLocalHostButtonReset, [this] { ui_->CommLocalHost->setText("0.0.0.0"); ui_->CommLocalPort->setText("15112"); });
    click(ui_->CommRemoteHostButtonReset, [this] { ui_->CommRemoteHost->setText("127.0.0.1"); ui_->CommRemotePort->setText("15112"); });
    click(ui_->SDLKeymapFilenameButtonReset, [this] { ui_->SDLKeymapFilename->setText("keymap.dat"); });
    click(ui_->DebuggerPortButtonReset, [this] { ui_->DebuggerPort->setText("23946"); });
    click(ui_->SaveStateNameButtonReset, [this] { ui_->SaveStateName->setText("%g"); });
    click(ui_->ShadowMaskTextureButtonReset, [this] { ui_->ShadowMaskTexture->setText("shadow-mask.png"); });
    click(ui_->HLSLIniFileButtonReset, [this] { ui_->HLSLIniFile->setText("%g"); });
    click(ui_->HLSLUpscaleSnapButtonReset, [this] { ui_->HLSLUpscaleSnapX->setText("2048"); ui_->HLSLUpscaleSnapY->setText("1536"); });
    click(ui_->BloomLUTTextureScreenButtonReset, [this] { ui_->BloomLUTTextureScreen->clear(); });
    click(ui_->BloomLUTTextureUIButtonReset, [this] { ui_->BloomLUTTextureUI->clear(); });
    click(ui_->AutobootCommandButtonClear, [this] { ui_->AutobootCommand->clear(); });
    click(ui_->SDLAttachWindowButtonClear, [this] { ui_->SDLAttachWindow->clear(); });

    // UI mode key: the next key pressed in the box is written as MAME's key name.
    auto* catcher = new KeyCatcher(ui_->UIModeKeyCustom);
    ui_->UIModeKeyCustom->installEventFilter(catcher);
    click(ui_->UIModeKeyButtonDetectKey, [catcher] { catcher->arm(); });

    // BGFX screen shader chains
    chainsModel_ = makeList(ui_->BGFXScreenShaderChains_ListView, {tr("Shader Chain"), tr("Name"), tr("Author")}, this);
    ui_->BGFXScreenShaderChains_ListView->header()->resizeSection(0, ui_->BGFXScreenShaderChains_ListView->width() / 2);
    connect(ui_->BGFXPath, &QLineEdit::editingFinished, this, [this] { populateShaderChains(); });
    auto selectedChain = [this] {
        QModelIndex i = ui_->BGFXScreenShaderChains_ListView->currentIndex();
        return i.isValid() ? chainsModel_->item(i.row(), 1)->text() : QString();
    };
    click(ui_->BGFXScreenShaderChainsButtonReset, [this] { ui_->BGFXScreenShaderChains->setText("default"); });
    click(ui_->BGFXScreenShaderChainsButtonReload, [this] { ui_->BGFXScreenShaderChains->setText(shaderChainsAtLoad_); });
    click(ui_->BGFXScreenShaderChains_ListViewButtonSelect, [=] {
        if (!selectedChain().isEmpty()) ui_->BGFXScreenShaderChains->setText(selectedChain());
    });
    click(ui_->BGFXScreenShaderChains_ListViewButtonAdd, [=] {
        QString c = selectedChain();
        if (c.isEmpty()) return;
        QString t = ui_->BGFXScreenShaderChains->text();
        ui_->BGFXScreenShaderChains->setText(t.isEmpty() ? c : t + "," + c);
    });
    connect(ui_->BGFXScreenShaderChains_ListView, &QTreeView::doubleClicked, this,
            [this] { ui_->BGFXScreenShaderChains_ListViewButtonSelect->click(); });
    click(ui_->BGFXScreenShaderChainsButtonSelect, [=] {
        QString path = ui_->BGFXPath->text().isEmpty() ? QString("bgfx") : ui_->BGFXPath->text();
        QString dir = resolvePath(fe_.mameFolder(sys_), path) + "/chains";
        if (!QFileInfo(dir).isDir()) return;
        QString f = QFileDialog::getOpenFileName(this, tr("Select a BGFX shader chain file"), dir, tr("Shader chains (*.json)"));
        if (!f.isEmpty()) ui_->BGFXScreenShaderChains->setText(QFileInfo(f).completeBaseName());
    });

    // Help
    click(ui_->VideoOutputModeButtonHelp, [this] {
        QMessageBox::information(this, tr("Video output mode"),
            tr("<b>OpenGL</b> video mode is only supported in SDLMAME and starting from MAME v0.159.<br>"
            "<b>DirectDraw</b> video mode is only supported in MAME v0.170 and lower versions.<br><br>"
            "In doubt, leave this setting in <b>Auto</b>."));
    });
    click(ui_->ButtonHelpSaveValidateAllCustomFiles, [this] {
        QMessageBox::information(this, tr("Validate All Custom Files on Save"),
            tr("This feature is used only when saving custom settings.<br><br>"
            "<b>Disabled / Unchecked</b><br>Custom settings are validated only against emulator default settings "
            "(mame.ini; hbmame.ini; ume.ini; emufilename.ini), ignoring all custom files.<br><br>"
            "<b>Enabled / Checked</b><br>It will keep custom settings files clean and avoid duplicated settings across "
            "files. All files will be scanned accordingly (debug.ini; vector.ini; drivername.ini; etc).<br>"
            "There's one major downside. Say you have settings in drivername.ini and gamename.ini. Then you decide to "
            "delete drivername.ini. All its settings will be lost as they are not listed in later files, forcing you "
            "to set them again in gamename.ini.<br><br>In doubt, keep this feature disabled/unchecked."));
    });
    click(ui_->ButtonReadFileHelp, [this] {
        QMessageBox::information(this, tr("What each button does?"),
            tr("Button <b>Reload Settings</b> will load a list of custom files one by one, overwriting settings from the "
            "previous one. MAME does this when loading games.<br><br>"
            "mame.ini; ume.ini; hbmame.ini; emufilename.ini<br>debug.ini<br>vertical.ini (if screen game is vertical)<br>"
            "horizont.ini (if screen game is horizontal)<br>arcade.ini (MAME v0.163 and newer)<br>console.ini (MAME v0.163 "
            "and newer)<br>computer.ini (MAME v0.163 and newer)<br>othersys.ini (MAME v0.163 and newer)<br>raster.ini (if "
            "screen game is raster)<br>vector.ini (if screen game is vector)<br>lcd.ini (if screen game is lcd)<br>svg.ini "
            "(if screen game is svg)<br>source\\sourcefile.ini or sourcefile.ini<br>biosname.ini<br>parentgame.ini (if "
            "current game is clone)<br>gamename.ini<br><br>Button <b>Reset to Default</b> will only load emulator default "
            "settings: mame.ini; ume.ini; hbmame.ini; ignoring everything else."));
    });
}

} // namespace eld
