// MainWindow: the tool bar's order and the Guide button. The tool bar shows the buttons in the
// order of [EmuLoaderRL] ToolBarOrder (button names, "|" for a separator, "space" for a space
// that pushes what follows to the right end); a button left out of it is hidden. [ToolBar]
// GamesFilters_Buttons (the original's: a 0/1 per button in its own order, then the search box)
// is kept in step, so the original still finds the buttons it knows shown or hidden.
#include "MainWindow.h"
#include "ui_FormMain.h"

#include "DiagnosticLog.h"
#include "ElWidgets.h"
#include "PreferencesDialog.h"
#include "SettingsDialog.h"
#include "ToolBarCustomizer.h"
#include "VclRuntime.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QMap>
#include <QMenu>
#include <QSet>
#include <QApplication>
#include <QKeyEvent>
#include <QTabWidget>

namespace eld {

QStringList MainWindow::toolOriginalOrder() const {
    QStringList out;
    for (ElButton* b : toolBarButtons()) out << b->objectName();
    return out;
}

QStringList MainWindow::toolDefaultOrder() const {
    return toolOriginalOrder() << QStringLiteral("space") << QStringLiteral("ButtonMameProblems") << QStringLiteral("ButtonGuide");
}

void MainWindow::loadToolOrder() {
    const QString saved = fe_.ini().readString("EmuLoaderRL", "ToolBarOrder");
    QStringList order;
    if (!saved.isEmpty()) {
        order = saved.split(',', Qt::SkipEmptyParts);
        // MAME Problems came later: added once, left of the Guide (then it's the user's to move)
        if (!order.contains("ButtonMameProblems") && !fe_.ini().readBool("EmuLoaderRL", "ToolBarProblemsAdded", false)) {
            const int guide = order.indexOf("ButtonGuide");
            order.insert(guide < 0 ? order.size() : guide, "ButtonMameProblems");
            fe_.ini().writeString("EmuLoaderRL", "ToolBarOrder", order.join(','));
        }
    } else { // first start: the original's buttons as GamesFilters_Buttons shows them, then the guide
        for (ElButton* b : toolBarButtons())
            if (b->property("elShown").toBool()) order << b->objectName();
        order << "space" << "ButtonMameProblems" << "ButtonGuide";
    }
    fe_.ini().writeBool("EmuLoaderRL", "ToolBarProblemsAdded", true);
    setToolOrder(order, false);
}

void MainWindow::setToolOrder(const QStringList& order, bool save) {
    QHash<QString, ElButton*> byName;
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>())
        if (b->objectName() != "ToolBarOverflow") byName.insert(b->objectName(), b);
    QStringList clean;
    QSet<QString> on;
    for (const QString& t : order) {
        if (t == "|" || t == "space") {
            clean << t;
            continue;
        }
        if (!byName.contains(t) || on.contains(t)) continue;
        on.insert(t);
        clean << t;
    }
    toolOrder_ = clean;
    for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
        ElButton* b = it.value();
        const bool shown = on.contains(it.key());
        b->setProperty("elShown", shown);
        b->setProperty("elOverflow", false);
        const bool can = (b != ui_->ButtonGameFilterMachineTypeSystemsMulti || PreferencesDialog::consoleComputerSupport(fe_)) &&
                         (b->objectName() != "ButtonMameProblems" || b->property("elActive").toBool()); // (only with problems to show)
        b->setVisible(shown && can);
    }
    qDeleteAll(toolSeparators_);
    toolSeparators_.clear();
    for (const QString& t : clean)
        if (t == "|") {
            auto* f = new QFrame(ui_->ToolBarButtons);
            f->setObjectName("ToolBarSeparator");
            f->setFrameShape(QFrame::VLine);
            f->setFrameShadow(QFrame::Sunken);
            toolSeparators_ << f;
        }
    if (save) {
        fe_.ini().writeString("EmuLoaderRL", "ToolBarOrder", clean.join(','));
        QString vis;
        for (ElButton* b : toolBarButtons()) vis += b->property("elShown").toBool() ? '1' : '0';
        vis += ui_->PanelSearchGames_ToolBar->isVisibleTo(ui_->PanelToolBar) ? '1' : '0';
        fe_.ini().writeString("ToolBar", "GamesFilters_Buttons", vis);
        fe_.saveIni();
    }
    layoutToolBar();
}

// The Guide button: a click opens the guide, a right-click its sections (as the Favorites and
// MAMu_ buttons' menus); F1 opens it too.
void MainWindow::setupGuideButton() {
    auto* b = new ElButton(ui_->ToolBarButtons);
    b->setObjectName("ButtonGuide");
    b->setProperty("elDesignText", tr("Guide-What's different, and how to use EmuLoader"));
    b->setFlat(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet("QPushButton { border: none; background: transparent; }"
                     "QPushButton:hover { background: rgba(0, 120, 215, 30); }"
                     "QPushButton:pressed { background: rgba(0, 120, 215, 60); }");
    setToolBarIcon(b, "information.ico", false, -1);
    b->setToolTip(tr("Open the guide (F1)\nRight-click for its sections"));
    connect(b, &QPushButton::clicked, this, [this] { openGuide(); });
    auto* menu = new QMenu(b);
    menu->setObjectName("GuideMenu");
    QAction* open = menu->addAction(tr("Open the Guide"), this, [this] { openGuide(); });
    QFont bold = open->font();
    bold.setBold(true);
    open->setFont(bold);
    menu->addSeparator();
    const std::pair<const char*, const char*> sections[] = {
        {QT_TR_NOOP("What's Different"), "differences"}, {QT_TR_NOOP("Getting Started"), "start"},
        {QT_TR_NOOP("Keyboard and Mouse"), "keys"}, {QT_TR_NOOP("Troubleshooting"), "trouble"}};
    for (const auto& [title, anchor] : sections) {
        const QString a = QString::fromLatin1(anchor);
        menu->addAction(tr(title), this, [this, a] { openGuide(a); });
    }
    b->setProperty("elMenu", QVariant::fromValue<QObject*>(menu)); // (the » menu: the sections)
    b->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(b, &QWidget::customContextMenuRequested, this, [b, menu] { menu->exec(b->mapToGlobal(QPoint(8, b->height()))); });
    // F1 in any of EmuLoader's windows: the guide, at that window's section
    qApp->installEventFilter(new GuideKey(this));
}

// The guide's section for a window: the Settings tab shown, or the window's own
QString MainWindow::guideAnchor(QWidget* window) {
    if (!window) return {};
    if (window->objectName() == "FormSettings")
        if (auto* tabs = window->findChild<QTabWidget*>("SettingsTabs"))
            if (QWidget* page = tabs->currentWidget()) return page->property("helpAnchor").toString();
    static const std::pair<const char*, const char*> forms[] = {
        {"FormArcadeScanGames", "scan"},          {"FormMAMESettings", "settings"},     {"FormFavoritesManager", "favorites"},
        {"FormGameDetails", "details"},           {"FormLastPlayedGames", "played"},    {"FormArcadeExportGamesList", "export"},
        {"FormImagesManager", "imagesmanager"},   {"FormArcadeROMsFolders", "romsfolders"}, {"FormArcadeFileVersions", "fileversions"},
        {"FormArcadeMultiSlotGames", "multislot"}, {"FormArcadeSoftwareList", "softlists"}, {"FormArcadeMAMEMachinesCustomize", "machinespanel"},
        {"FormCustomParameters", "params"},       {"FormArcadeRunGameExtraMAME", "extra"}, {"FormArcadeScanAudioSamples", "samples"},
        {"FormArcadeGamesFilter", "search"},      {"FormArcadeFiltersExtra", "search"}, {"FormArcadeFiltersDriverStatus", "search"},
        {"FormCopyMoveGameFiles", "gamefiles"},   {"FormDeleteGameFiles", "gamefiles"}, {"FormArcadeMAMu_", "mamu"},
        {"FormColumnsEditor", "list"},            {"FormGamesListFontSettings", "list"}};
    for (QWidget* w = window; w; w = w->parentWidget())
        for (const auto& [prefix, anchor] : forms)
            if (w->objectName().startsWith(QLatin1String(prefix))) return QString::fromLatin1(anchor);
    return {};
}

bool GuideKey::eventFilter(QObject* o, QEvent* e) {
    if (e->type() != QEvent::KeyPress || !o->isWidgetType()) return false;
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() != Qt::Key_F1 || k->modifiers() != Qt::NoModifier || k->isAutoRepeat()) return false;
    QWidget* w = static_cast<QWidget*>(o)->window();
    if (!w || w->isHidden()) return false;
    const QString anchor = MainWindow::guideAnchor(w);
    DiagnosticLog::write("guide", "F1 in " + w->objectName() + (anchor.isEmpty() ? QString() : " -> #" + anchor));
    main_->openGuide(anchor);
    return true;
}

// The main CPU filter (PanelFilterCPU; Misc Filters > Filter MAME Games by Main CPU): a row under
// the tool bar with every main CPU (and how many games have it), or part of a CPU's name; the
// games list shows MAME's machines with that CPU, with the other filters. Close hides the row and
// turns the Misc filter off, as the original.
void MainWindow::setupCpuFilter() {
    QWidget* row = ui_->PanelFilterCPU;
    row->setProperty("vclAlign", "alBottom");
    row->setFixedHeight(40);
    for (QWidget* w : row->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) w->hide();
    // the controls in a container of their own, laid out (the panel places its own children where the
    // original's form had them)
    auto* inner = new QWidget(row);
    inner->setObjectName("FilterCPURow");
    inner->setProperty("vclAlign", "alClient");
    inner->setGeometry(0, 0, row->width(), 40);
    inner->show();
    auto* close = new QToolButton(row);
    close->setObjectName("FilterCPUClose");
    close->setText(QStringLiteral("\u2715"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close the CPU filter (and turn it off in Misc Filters)"));
    auto* label = new QLabel(tr("Main CPU"), row); // (plain labels: the form's draw at their original size)
    label->setObjectName("FilterCPULabel");
    QComboBox* list = ui_->FilterCPUList;
    list->setMinimumWidth(240);
    list->setMaxVisibleItems(25);
    list->setToolTip(tr("The games whose main CPU is this one"));
    auto* or_ = new QLabel(tr("or a Name Containing"), row);
    or_->setObjectName("FilterCPUNameLabel");
    QLineEdit* ed = ui_->FilterCPU;
    ed->setMinimumWidth(180);
    ed->setClearButtonEnabled(true);
    ed->setPlaceholderText(tr("68000, Z80, SH-2..."));
    ed->setToolTip(tr("The games whose main CPU has this in its name; press Enter"));
    for (auto [b, file, tip] : {std::tuple{ui_->ButtonFilterCPUApply, "search.ico", QT_TR_NOOP("Apply")},
                                std::tuple{ui_->ButtonFilterCPUReset, "refresh.ico", QT_TR_NOOP("Reset: all CPUs again")}}) {
        b->setIcon(toolIcon(file, false));
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(30, 28);
        b->setToolTip(tr(tip));
        b->setEnabled(true);
    }
    auto* note = new QLabel(row);
    note->setObjectName("FilterCPUNote");
    note->setStyleSheet("color: #B00000;");
    auto* h = new QHBoxLayout(inner);
    h->setContentsMargins(6, 4, 6, 4);
    h->setSpacing(6);
    for (QWidget* w : std::initializer_list<QWidget*>{close, label, list, or_, ed, ui_->ButtonFilterCPUApply, ui_->ButtonFilterCPUReset, note}) {
        vcl::stopEliding(w);
        h->addWidget(w);
        w->show();
    }

    h->addStretch(1);
    connect(list, &QComboBox::activated, this, [this, list, ed](int i) {
        ed->clear();
        applyCpuFilter(i > 0 ? list->itemData(i).toString() : QString(), true);
    });
    auto apply = [this, list, ed] {
        const QString t = ed->text().trimmed();
        if (!t.isEmpty()) {
            list->setCurrentIndex(0);
            applyCpuFilter(t, false);
        } else {
            applyCpuFilter(list->currentIndex() > 0 ? list->currentData().toString() : QString(), true);
        }
    };
    connect(ed, &QLineEdit::returnPressed, this, apply);
    connect(ui_->ButtonFilterCPUApply, &QPushButton::clicked, this, apply);
    connect(ui_->ButtonFilterCPUReset, &QPushButton::clicked, this, [this, list, ed] {
        ed->clear();
        list->setCurrentIndex(0);
        applyCpuFilter({}, true);
    });
    connect(close, &QToolButton::clicked, this, [this] {
        MiscFilters f = filter_->miscFilters();
        f.mainCpu = false;
        f.save(fe_.gamesFiltersFile());
        filter_->setMiscFilters(f, multiSlot_);
        showCpuFilter(false);
    });
}

void MainWindow::applyCpuFilter(const QString& cpu, bool exact) {
    if (cpu == filter_->cpuFilter()) return;
    rememberSelection();
    filter_->setCpuFilter(cpu, exact);
    DiagnosticLog::write("filters", cpu.isEmpty() ? QString("CPU filter off") : QString("CPU filter: %1%2").arg(cpu, exact ? "" : " (in the name)"));
    afterFilterChange();
}

// The main CPUs of the games list, with how many games have each; a list made before this version
// has none (it says so)
void MainWindow::fillCpuList() {
    QMap<QString, int> count; // (sorted without case below)
    for (const MameGame& g : db_.games())
        if (!g.mainCpu.isEmpty() && !g.isSoftware()) ++count[g.mainCpu];
    QStringList names = count.keys();
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    QComboBox* list = ui_->FilterCPUList;
    const QString cur = list->currentData().toString();
    QSignalBlocker block(list);
    list->clear();
    list->addItem(tr("(All CPUs)"), QString());
    for (const QString& n : names) list->addItem(QString("%1 (%2)").arg(n, QLocale().toString(count.value(n))), n);
    list->setCurrentIndex(std::max(0, list->findData(cur)));
    const bool known = !names.isEmpty() || db_.games().isEmpty();
    for (QWidget* w : std::initializer_list<QWidget*>{list, ui_->FilterCPU, ui_->ButtonFilterCPUApply, ui_->ButtonFilterCPUReset})
        w->setEnabled(known);
    if (auto* note = ui_->PanelFilterCPU->findChild<QLabel*>("FilterCPUNote")) {
        note->setText(known ? QString() : tr("Create the games list again (Menu Options > MAME/Arcade > Create Arcade Games List) "
                                             "to use the CPU filter."));
        note->setVisible(!known);
    }
}

void MainWindow::showCpuFilter(bool on) {
    if (!on && !filter_->cpuFilter().isEmpty()) applyCpuFilter({}, true);
    if (!on) {
        ui_->FilterCPU->clear();
        ui_->FilterCPUList->setCurrentIndex(0);
    }
    ui_->PanelFilterCPU->setVisible(on);
    layoutToolBar();
}

// Settings > Tool Bar: the buttons and their order (drag and drop), presets, the options. Each
// change shows at once; Cancel puts back the tool bar as it was.
void MainWindow::addToolBarPage(SettingsDialog& s, AfterSettings&) {
    ToolBarCustomizer::Input in;
    for (ElButton* b : ui_->ToolBarButtons->findChildren<ElButton*>()) {
        if (b->objectName() == "ToolBarOverflow") continue;
        QString text = b->property("elDesignText").toString();
        if (text.isEmpty()) text = b->toolTip().section('\n', 0, 0); // (Menu Options has no caption)
        ToolBarCustomizer::Tool t;
        t.id = b->objectName();
        t.title = text.section('-', 0, 0).trimmed();
        t.detail = text.section('-', 1).trimmed();
        if (b == ui_->ButtonGameFilterMachineTypeSystemsMulti && !PreferencesDialog::consoleComputerSupport(fe_))
            t.detail = tr("Needs console support");
        t.icon = b->icon();
        t.caption = b->caption();
        in.tools << t;
    }
    in.order = toolOrder_;
    in.original = toolOriginalOrder();
    in.defaults = toolDefaultOrder();
    in.presetsFile = fe_.userFile("toolbar_presets.ini");
    QAction* show = menus_.action("MenuEnableToolBar");
    QAction* bound = menus_.action("MenuBoundToGamesPanel");
    in.showBar = !show || show->isChecked();
    in.bound = bound && bound->isChecked();
    in.showSearch = ui_->PanelSearchGames_ToolBar->isVisibleTo(ui_->PanelToolBar);
    in.iconSize = fe_.ini().readInteger("ToolBar", "GamesFilters_IconSize", 1);
    in.setOrder = [this](const QStringList& o) { setToolOrder(o); };
    in.setShowBar = [this, show](bool on) {
        if (show) show->setChecked(on);
        applyToolBarPlacement();
    };
    in.setBound = [this, bound](bool on) {
        if (bound) bound->setChecked(on);
        applyToolBarPlacement();
    };
    in.setShowSearch = [this](bool on) {
        ui_->PanelSearchGames_ToolBar->setVisible(on);
        vcl::relayout(ui_->PanelToolBar);
        setToolOrder(toolOrder_); // (saves GamesFilters_Buttons' last flag)
    };
    in.setIconSize = [this](int i) {
        const char* sizeMenus[] = {"MenuToolBarIconSizeExtraLarge", "MenuToolBarIconSizeLarge", "MenuToolBarIconSizeSmall"};
        if (QAction* a = menus_.action(sizeMenus[i])) a->setChecked(true);
        setToolBarIconSize(i);
    };
    auto* page = new ToolBarCustomizer(in);
    const ToolBarCustomizer::Input before = in;
    s.addPage(tr("Tool Bar"), page, {}, [this, before] { // Cancel: the tool bar as it was
        setToolOrder(before.order);
        before.setShowSearch(before.showSearch);
        before.setShowBar(before.showBar);
        before.setBound(before.bound);
        before.setIconSize(before.iconSize);
    });
}

} // namespace eld
