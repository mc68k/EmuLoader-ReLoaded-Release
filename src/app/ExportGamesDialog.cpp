#include "ExportGamesDialog.h"
#include "FileProblem.h"

#include "GamesModel.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "ui_FormArcadeExportGamesList.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QSet>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eld {

namespace {

const char* const kSection = "ExportArcadeGames";
// Game Info To Export: the games list column of each item (aColumns), -1 = the system's name;
// in the original's default order (GameInfoListToExport_DefaultClick).
const int kDefaultInfo[] = {-1, GamesModel::Title, GamesModel::Year, GamesModel::Manufacturer, GamesModel::Name,
                            GamesModel::Clone, GamesModel::SoftwareName, GamesModel::Orientation, GamesModel::Players,
                            GamesModel::Category};
// the columns that can be exported (with "Use Games List Columns", the ones shown of these)
const int kExportable[] = {GamesModel::Title, GamesModel::Year, GamesModel::Manufacturer, GamesModel::Orientation,
                           GamesModel::Category, GamesModel::Name, GamesModel::Clone, GamesModel::Players,
                           GamesModel::SoftwareName};

QString infoTitle(int column) {
    switch (column) {
    case -1: return QCoreApplication::translate("eld::ExportGamesDialog", "System Name");
    case GamesModel::Orientation: return QCoreApplication::translate("eld::ExportGamesDialog", "Screen Orientation");
    case GamesModel::Category: return QCoreApplication::translate("eld::ExportGamesDialog", "Category");
    case GamesModel::Players: return QCoreApplication::translate("eld::ExportGamesDialog", "Number of Players");
    default: return GamesModel::columnTitle(column);
    }
}

// the first line's titles (the original writes "Screen", "Players", "System" there)
QString headerTitle(int column) {
    switch (column) {
    case -1: return QCoreApplication::translate("eld::ExportGamesDialog", "System");
    case GamesModel::Orientation: return QCoreApplication::translate("eld::ExportGamesDialog", "Screen");
    case GamesModel::Players: return QCoreApplication::translate("eld::ExportGamesDialog", "Players");
    default: return GamesModel::columnTitle(column);
    }
}

} // namespace

ExportGamesDialog::ExportGamesDialog(const Source& src, const QString& extrasIni, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeExportGamesList), src_(src), extrasIni_(extrasIni) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Export MAME and Arcade Games List"));

    ui_->ExportOption_ArcadeGames->setText(tr("Arcade Games"));
    ui_->LabelSelectMode->setText(tr("Choose the output format. AntoPISA's \"mess.ini\" file is required for non-arcade "
                                     "machines. You can customize the game info to export (\"Full Format\" only)."));

    // ExportList: the three formats, three choices each
    modes_ = new QTreeWidget(ui_->PanelExportList);
    modes_->setHeaderHidden(true);
    modes_->setRootIsDecorated(false);
    modes_->setIndentation(12);
    modes_->setFrameShape(QFrame::NoFrame);
    const struct { QString group; Mode modes[3]; } groups[] = {
        {tr("Full Format"), {FullAll, FullShown, FullSelected}},
        {tr("Short Format: gamename=system"), {ShortAll, ShortShown, ShortSelected}},
        {tr("MAME Content Manager Plus"), {McmAll, McmShown, McmSelected}}};
    const QString names[] = {tr("Export All Games"), tr("Export Visible Games"), tr("Export Selected Games")};
    for (const auto& g : groups) {
        auto* top = new QTreeWidgetItem(modes_, {g.group});
        top->setFlags(Qt::ItemIsEnabled);
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        for (int i = 0; i < 3; ++i) {
            auto* it = new QTreeWidgetItem(top, {names[i]});
            it->setData(0, Qt::UserRole, int(g.modes[i]));
        }
    }
    modes_->expandAll();
    connect(modes_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it->parent()) generate();
    });

    // Game Info To Export: checkable, in order
    info_ = new QListWidget(ui_->GroupExportOptionsGameColumnsFullFormat);
    connect(info_, &QListWidget::itemChanged, this, [](QListWidgetItem* it) { // unchecked: gray (GameInfoListToExportItemPaintText)
        it->setForeground(it->checkState() == Qt::Checked ? QBrush() : QBrush(Qt::gray));
    });
    defaultColumns();

    connect(ui_->GameInfoListToExport_MoveUp, &QPushButton::clicked, this, [this] { moveColumn(-1); });
    connect(ui_->GameInfoListToExport_MoveDown, &QPushButton::clicked, this, [this] { moveColumn(1); });
    connect(ui_->GameInfoListToExport_Reset, &QPushButton::clicked, this, [this] { readSettings(true); });
    connect(ui_->GameInfoListToExport_Default, &QPushButton::clicked, this, &ExportGamesDialog::defaultColumns);
    connect(ui_->GameInfoToExport_UseGamesListVisibleColumns, &QCheckBox::toggled, info_, &QWidget::setDisabled);
    connect(ui_->ButtonHelp_GameInfoToExport_MicrosoftExcelFormat, &QPushButton::clicked, this, &ExportGamesDialog::excelHelp);
    connect(ui_->ButtonHelp, &QPushButton::clicked, this, &ExportGamesDialog::help);
    connect(ui_->ButtonApply, &QPushButton::clicked, this, [this] { generate(); });
    connect(ui_->ButtonApplyAndExit, &QPushButton::clicked, this, [this] { if (generate()) accept(); });
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, this, [this] { writeSettings(); }); // FormCloseQuery
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    layoutWindow();

    readSettings();
    setMode(FullAll);
    modes_->setFocus();
}

ExportGamesDialog::~ExportGamesDialog() = default;

// The window in layouts: the explanation on top; the formats, what to export and the game info
// side by side; Help, Apply and Exit under them. Longer translations and larger text get the room
// they need, and the window can be made bigger (the lists take the room).
void ExportGamesDialog::layoutWindow() {
    using vcl::boxed, vcl::loose;
    auto box = boxed;
    auto button = vcl::textButton;
    // the explanation
    QWidget* top = ui_->TopBar;
    loose(top);
    loose(ui_->LabelSelectMode);
    ui_->LabelSelectMode->setWordWrap(true);
    ui_->LabelSelectMode->setMinimumWidth(560); // (its lines reckoned at about the design's width)
    box(new QVBoxLayout(top), 16, 8, 16, 7, 0)->addWidget(ui_->LabelSelectMode);
    top->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // the formats, and MAME Content Manager Plus under them
    QWidget* formats = ui_->PanelExportList;
    loose(formats);
    box(new QVBoxLayout(formats), 2, 2, 2, 2, 0)->addWidget(modes_);
    formats->setMinimumSize(249, 200);
    QGroupBox* mcm = ui_->GroupMAMEContentManagerPlus;
    loose(mcm);
    auto* m = box(new QHBoxLayout(mcm), 8, 4, 8, 4, 20);
    for (QWidget* c : {(QWidget*)ui_->MCMPlus_MAME, (QWidget*)ui_->MCMPlus_HBMAME}) {
        loose(c);
        m->addWidget(c);
    }
    m->addStretch();
    auto* left = box(new QVBoxLayout, 0, 0, 0, 0, 5);
    left->addWidget(formats, 1);
    left->addWidget(mcm);

    // what to export
    QGroupBox* what = ui_->GroupExportOptionsAllGames;
    loose(what);
    auto* w = box(new QVBoxLayout(what), 8, 4, 8, 8, 2);
    for (QWidget* c : {(QWidget*)ui_->ExportOption_ArcadeGames, (QWidget*)ui_->ExportOption_NonArcadeMAMEMachines,
                       (QWidget*)ui_->ExportOption_MAMESoftwareListGames}) {
        loose(c);
        w->addWidget(c);
    }
    loose(ui_->LabelExportOption_MAMESoftwareListGames);
    auto* note = box(new QHBoxLayout, 15, 0, 0, 0, 0);
    note->addWidget(ui_->LabelExportOption_MAMESoftwareListGames);
    w->addLayout(note);
    w->addSpacing(4);
    for (QWidget* c : {(QWidget*)ui_->ExportOption_MAME, (QWidget*)ui_->ExportOption_HBMAME}) {
        loose(c);
        w->addWidget(c);
    }
    w->addStretch();
    w->addStrut(247); // (the design's width at least; a minimum width of the box itself would hide its contents' own)

    // the game info to export
    QGroupBox* info = ui_->GroupExportOptionsGameColumnsFullFormat;
    loose(info);
    auto* g = box(new QVBoxLayout(info), 8, 4, 8, 6, 4);
    loose(info_);
    info_->setMinimumHeight(120);
    g->addWidget(info_, 1);
    auto* order = box(new QHBoxLayout, 0, 0, 0, 0, 2);
    for (QPushButton* b : {ui_->GameInfoListToExport_MoveUp, ui_->GameInfoListToExport_MoveDown,
                           ui_->GameInfoListToExport_Reset, ui_->GameInfoListToExport_Default})
        order->addWidget(button(b), 1);
    g->addLayout(order);
    g->addSpacing(4);
    loose(ui_->GameInfoToExport_UseGamesListVisibleColumns);
    g->addWidget(ui_->GameInfoToExport_UseGamesListVisibleColumns);
    auto* excel = box(new QHBoxLayout, 0, 0, 0, 0, 4);
    loose(ui_->GameInfoToExport_MicrosoftExcelFormat);
    excel->addWidget(ui_->GameInfoToExport_MicrosoftExcelFormat, 1);
    excel->addWidget(button(ui_->ButtonHelp_GameInfoToExport_MicrosoftExcelFormat));
    ui_->ButtonHelp_GameInfoToExport_MicrosoftExcelFormat->setFixedWidth(ui_->ButtonHelp_GameInfoToExport_MicrosoftExcelFormat->minimumWidth());
    g->addLayout(excel);
    g->addStrut(217);

    auto* middle = box(new QHBoxLayout, 8, 8, 8, 6, 9);
    middle->addLayout(left, 1);
    middle->addWidget(what, 1);
    middle->addWidget(info, 1);

    // Help; Apply and Exit, Apply; Exit
    QWidget* bottom = ui_->PanelBottom;
    loose(bottom);
    bottom->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* b = box(new QHBoxLayout(bottom), 8, 8, 8, 8, 8);
    b->addWidget(button(ui_->ButtonHelp));
    b->addStretch(1);
    b->addWidget(button(ui_->ButtonApplyAndExit));
    b->addWidget(button(ui_->ButtonApply));
    b->addStretch(1);
    b->addWidget(button(ui_->ButtonCancel));

    auto* root = box(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(top);
    root->addLayout(middle, 1);
    root->addWidget(bottom);
    // (laid out here: the design's places are left behind)
    for (QWidget* x : {(QWidget*)this, top, formats, (QWidget*)mcm, (QWidget*)what, (QWidget*)info, bottom})
        x->setProperty("vclLaidOut", true);
    layout()->activate();
    resize(QSize(780, 404).expandedTo(minimumSizeHint())); // (the window as it was)
}

void ExportGamesDialog::setMode(Mode m) {
    for (int g = 0; g < modes_->topLevelItemCount(); ++g)
        for (int i = 0; i < modes_->topLevelItem(g)->childCount(); ++i) {
            QTreeWidgetItem* it = modes_->topLevelItem(g)->child(i);
            if (it->data(0, Qt::UserRole).toInt() == m) modes_->setCurrentItem(it);
        }
}

ExportGamesDialog::Mode ExportGamesDialog::mode() const {
    QTreeWidgetItem* it = modes_->currentItem();
    return it && it->parent() ? Mode(it->data(0, Qt::UserRole).toInt()) : FullAll;
}

void ExportGamesDialog::defaultColumns() {
    info_->clear();
    for (int c : kDefaultInfo) {
        auto* it = new QListWidgetItem(infoTitle(c), info_);
        it->setData(Qt::UserRole, c);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
    }
}

void ExportGamesDialog::moveColumn(int by) { // ELV_MoveItem
    int r = info_->currentRow(), to = r + by;
    if (r < 0 || to < 0 || to >= info_->count()) return;
    QListWidgetItem* it = info_->takeItem(r);
    info_->insertItem(to, it);
    info_->setCurrentRow(to);
}

void ExportGamesDialog::readSettings(bool columnsOnly) {
    WinIni ini;
    ini.load(extrasIni_); // missing: the defaults
    if (!columnsOnly) {
        auto set = [&](QCheckBox* c, const char* key, int def = 1) { c->setChecked(ini.readInteger(kSection, key, def) != 0); };
        set(ui_->ExportOption_ArcadeGames, "ExportAllGamesOption_ArcadeGames");
        set(ui_->ExportOption_NonArcadeMAMEMachines, "ExportAllGamesOption_NonArcadeMAMEMachines");
        set(ui_->ExportOption_MAMESoftwareListGames, "ExportAllGamesOption_MAMESoftwareListGames");
        set(ui_->ExportOption_MAME, "ExportAllGamesOption_MAME");
        set(ui_->ExportOption_HBMAME, "ExportAllGamesOption_HBMAME");
        set(ui_->MCMPlus_MAME, "MCMPlus_MAME");
        set(ui_->MCMPlus_HBMAME, "MCMPlus_HBMAME");
        set(ui_->GameInfoToExport_UseGamesListVisibleColumns, "GameInfoToExport_UseGamesListVisibleColumns", 0);
        set(ui_->GameInfoToExport_MicrosoftExcelFormat, "GameInfoToExport_MicrosoftExcelFormat", 1);
        info_->setDisabled(ui_->GameInfoToExport_UseGamesListVisibleColumns->isChecked());
    }
    // GameInfoToExport_Item01... (the column), GameInfoToExport_CheckedItem01...
    QList<QPair<int, bool>> items;
    QSet<int> seen;
    for (int i = 1; i <= int(std::size(kDefaultInfo)); ++i) {
        QString n = QString("%1").arg(i, 2, 10, QChar('0'));
        int c = ini.readInteger(kSection, "GameInfoToExport_Item" + n, kDefaultInfo[i - 1]);
        bool known = std::find(std::begin(kDefaultInfo), std::end(kDefaultInfo), c) != std::end(kDefaultInfo);
        if (!known || seen.contains(c)) return; // not a list this version knows: keep the default
        seen.insert(c);
        items.append({c, ini.readInteger(kSection, "GameInfoToExport_CheckedItem" + n, 1) != 0});
    }
    info_->clear();
    for (auto [c, on] : items) {
        auto* it = new QListWidgetItem(infoTitle(c), info_);
        it->setData(Qt::UserRole, c);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
}

void ExportGamesDialog::writeSettings() const {
    WinIni ini;
    ini.load(extrasIni_);
    auto put = [&](const QCheckBox* c, const char* key) { ini.writeInteger(kSection, key, c->isChecked() ? 1 : 0); };
    put(ui_->ExportOption_ArcadeGames, "ExportAllGamesOption_ArcadeGames");
    put(ui_->ExportOption_NonArcadeMAMEMachines, "ExportAllGamesOption_NonArcadeMAMEMachines");
    put(ui_->ExportOption_MAMESoftwareListGames, "ExportAllGamesOption_MAMESoftwareListGames");
    put(ui_->ExportOption_MAME, "ExportAllGamesOption_MAME");
    put(ui_->ExportOption_HBMAME, "ExportAllGamesOption_HBMAME");
    put(ui_->MCMPlus_MAME, "MCMPlus_MAME");
    put(ui_->MCMPlus_HBMAME, "MCMPlus_HBMAME");
    for (int i = 0; i < info_->count(); ++i) {
        QString n = QString("%1").arg(i + 1, 2, 10, QChar('0'));
        ini.writeInteger(kSection, "GameInfoToExport_Item" + n, info_->item(i)->data(Qt::UserRole).toInt());
        ini.writeInteger(kSection, "GameInfoToExport_CheckedItem" + n, info_->item(i)->checkState() == Qt::Checked ? 1 : 0);
    }
    put(ui_->GameInfoToExport_UseGamesListVisibleColumns, "GameInfoToExport_UseGamesListVisibleColumns");
    put(ui_->GameInfoToExport_MicrosoftExcelFormat, "GameInfoToExport_MicrosoftExcelFormat");
    ini.save(extrasIni_);
}

// GenerateFile: the lines of the file
QStringList ExportGamesDialog::lines() const {
    const Mode m = mode();
    GamesModel* model = src_.model;
    GamesFilter* filter = src_.filter;
    const QString system = Frontend::section(src_.system);           // "MAME"
    const QString systemKey = Frontend::fileBase(src_.system);       // "mame"
    // the games: all of the list, the ones shown (in their order), or the selected ones
    QList<int> rows;
    if (m == FullAll || m == ShortAll || m == McmAll) {
        for (int r = 0; r < model->rowCount(); ++r) rows << r;
    } else if (m == FullShown || m == ShortShown || m == McmShown) {
        for (int r = 0; r < filter->rowCount(); ++r) rows << filter->mapToSource(filter->index(r, 0)).row();
    } else {
        QModelIndexList sel = src_.list->selectionModel()->selectedRows();
        std::sort(sel.begin(), sel.end(), [](const QModelIndex& a, const QModelIndex& b) { return a.row() < b.row(); });
        for (const QModelIndex& i : sel) rows << filter->mapToSource(i).row();
    }
    // ValidateGameFilter: Export All Games' choices (the system, arcade or not)
    auto keep = [&](const MameGame& g) {
        if (m != FullAll && m != ShortAll) return true;
        bool sys = src_.system == SystemId::MAME ? ui_->ExportOption_MAME->isChecked() : ui_->ExportOption_HBMAME->isChecked();
        if (!sys) return false;
        if (g.isSoftware()) return ui_->ExportOption_MAMESoftwareListGames->isChecked();
        return g.systemType == 0 ? ui_->ExportOption_ArcadeGames->isChecked() : ui_->ExportOption_NonArcadeMAMEMachines->isChecked();
    };
    QStringList out;
    if (m == McmAll || m == McmShown || m == McmSelected) { // MAME Content Manager Plus: set names
        bool on = src_.system == SystemId::MAME ? ui_->MCMPlus_MAME->isChecked() : ui_->MCMPlus_HBMAME->isChecked();
        if (!on) return out;
        for (int r : rows)
            if (!model->game(r).isSoftware()) out << model->game(r).name; // MAME's sets only
        return out;
    }
    if (m == ShortAll || m == ShortShown || m == ShortSelected) { // gamename=system
        for (int r : rows)
            if (keep(model->game(r))) out << model->game(r).key() + '=' + systemKey; // "nes\\smb=mame" for software
        return out;
    }
    // Full Format: the info chosen, or the games list's visible columns (of those that can be exported)
    QList<int> cols;
    if (ui_->GameInfoToExport_UseGamesListVisibleColumns->isChecked()) {
        QHeaderView* h = src_.list->header();
        for (int v = 0; v < h->count(); ++v) {
            int c = h->logicalIndex(v);
            if (!h->isSectionHidden(c) && std::find(std::begin(kExportable), std::end(kExportable), c) != std::end(kExportable))
                cols << c;
        }
    } else {
        for (int i = 0; i < info_->count(); ++i)
            if (info_->item(i)->checkState() == Qt::Checked) cols << info_->item(i)->data(Qt::UserRole).toInt();
    }
    const bool excel = ui_->GameInfoToExport_MicrosoftExcelFormat->isChecked();
    auto line = [&](const QStringList& values) { // MountExcelFormatStr
        if (excel) {
            QStringList q;
            for (QString v : values) q << '"' + v.replace('"', "\"\"") + '"';
            return q.join(',');
        }
        QStringList nonEmpty;
        for (const QString& v : values) if (!v.isEmpty()) nonEmpty << v;
        return nonEmpty.join(" - ");
    };
    QStringList titles;
    for (int c : cols) titles << headerTitle(c);
    for (int r : rows) {
        const MameGame& g = model->game(r);
        if (!keep(g)) continue;
        QStringList v;
        for (int c : cols) {
            if (c == -1) v << system;
            else if (c == GamesModel::Title) v << model->title(g);
            else v << model->data(model->index(r, c), Qt::DisplayRole).toString();
        }
        out << line(v);
    }
    if (!out.isEmpty()) out.prepend(line(titles)); // the column titles first
    return out;
}

bool ExportGamesDialog::write(const QString& file, QString* error) const {
    const QStringList l = lines();
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QByteArray data;
    const bool full = mode() == FullAll || mode() == FullShown || mode() == FullSelected;
    if (full && ui_->GameInfoToExport_MicrosoftExcelFormat->isChecked()) data = "\xEF\xBB\xBF"; // so Excel reads it as UTF-8
    for (const QString& s : l) data += s.toUtf8() + "\r\n";
    f.write(data);
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

bool ExportGamesDialog::generate() {
    if (!modes_->currentItem() || !modes_->currentItem()->parent()) return false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const int count = int(lines().size());
    QApplication::restoreOverrideCursor();
    if (count == 0) {
        QMessageBox::information(this, windowTitle(), tr("There are no games to export with these choices."));
        return false;
    }
    QString file = testFile_;
    if (file.isEmpty()) { // DialogSaveFile(4, 'Save Games List To a File')
        const bool csv = (mode() == FullAll || mode() == FullShown || mode() == FullSelected) &&
                         ui_->GameInfoToExport_MicrosoftExcelFormat->isChecked();
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        file = QFileDialog::getSaveFileName(this, tr("Save Games List To a File"),
                                            QDir(dir).filePath(Frontend::fileBase(src_.system) + (csv ? "_games.csv" : "_games.txt")),
                                            csv ? tr("CSV files (*.csv);;Text files (*.txt);;All files (*.*)")
                                                : tr("Text files (*.txt);;All files (*.*)"));
        if (file.isEmpty()) return false;
    }
    QString err;
    if (!write(file, &err)) {
        FileProblem::cantSave(this, windowTitle(), file);
        return false;
    }
    return true;
}

void ExportGamesDialog::help() {
    QMessageBox::information(this, tr("Help"), tr(
        "<p>Use this feature to create a <b>.txt</b> file of the current games list. There are three output formats to "
        "choose from:</p>"
        "<p><b>Full Format</b>: suitable for a file viewer or Microsoft Excel. The <b>Game Info To Export</b> settings are "
        "only used in this format: you can choose which game info is included, and in what order. Optionally, the file "
        "can be made in a format that Excel can import, by checking <b>Microsoft Excel Format</b>.</p>"
        "<p><b>Short Format</b>: gamename=system lines, to make favorites profiles quickly, for example.</p>"
        "<p><b>MAME Content Manager Plus</b>: a file for the MCM+ application, with set names only.</p>"
        "<p>Each format has three choices:</p>"
        "<p><b>Export All Games</b>: every game of the list, shown or not. <b>Export All Games</b> (the options panel) "
        "chooses which systems and kinds of games are included.<br>"
        "<b>Export Visible Games</b>: only the games shown in the main games list: set its filters first.<br>"
        "<b>Export Selected Games</b>: only the games you selected in the main games list.</p>"
        "<p>All the settings are kept in el_extras.ini, so you won't have to set them again.</p>"));
}

void ExportGamesDialog::excelHelp() {
    QMessageBox::information(this, tr("Microsoft Excel Format"), tr(
        "<p>Makes a games list that Microsoft Excel can <b>Import</b> (Data &gt; From Text/CSV).</p>"
        "<p>Choose these in Excel:<br>1. <b>Comma</b> as the delimiter<br>2. <b>Double Quote</b> as the text "
        "qualifier</p><p>Each value is in double quotes because a text or a number can have a comma in it. A sample:</p>"
        "<pre>\"Title\",\"Year\",\"Game Name\",\"Manufacturer\"\n\"Ragnagard / Shin-Oh-Ken\",\"1996\",\"ragnagrd\",\"Saurus\"\n"
        "\"Raiden (Rev. A)\",\"1994\",\"raiden\",\"Atari\"</pre>"));
}

} // namespace eld
