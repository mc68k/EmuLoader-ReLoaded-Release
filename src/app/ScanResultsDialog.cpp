#include "ScanResultsDialog.h"
#include "FileProblem.h"

#include "ChdCheck.h"
#include "ElWidgets.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "ui_FormArcadeScanGamesResults.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eld {

namespace {
enum Col { CName, CCrc, CSha1, CSize, CDevice, CStatus, ColCount };
enum Role { KeyRole = Qt::UserRole + 1, NotFoundRole, ChdRole, BiosRole, DeviceRole, SoftwareRole, SortRole, CommandRole, WhyRole };

QString hashPart(const QString& hashes, const QString& which) { // "CRC(c1e6ab10) SHA1(e87e...)" -> "c1e6ab10"
    const QRegularExpression re(which + QStringLiteral(R"(\(([0-9a-fA-F]+)\))"));
    const auto m = re.match(hashes);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString problemText(const RomAudit::File& f) {
    switch (f.problem) {
    case RomAudit::NotFound: return f.kind == RomAudit::Disk ? ScanResultsDialog::tr("CHD not found") : ScanResultsDialog::tr("Not found");
    case RomAudit::BadChecksum: return ScanResultsDialog::tr("Incorrect checksum");
    case RomAudit::BadLength: { // "INCORRECT LENGTH: 2048 bytes"
        const QRegularExpression n(QStringLiteral(R"((\d+) bytes)"));
        const auto m = n.match(f.text);
        return m.hasMatch() ? ScanResultsDialog::tr("Incorrect length (%1 bytes)").arg(QLocale().toString(m.captured(1).toLongLong()))
                            : ScanResultsDialog::tr("Incorrect length");
    }
    default: return f.text;
    }
}

// Enter selects the game in the games list
class ResultsTree : public QTreeWidget {
public:
    std::function<void()> onEnter;
    using QTreeWidget::QTreeWidget;
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && onEnter) { onEnter(); return; }
        QTreeWidget::keyPressEvent(e);
    }
};
} // namespace

ScanResultsDialog::ScanResultsDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeScanGamesResults), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(in_.only.isEmpty() ? tr("View Missing ROMs/CHDs") : tr("View Missing ROMs/CHDs: %1").arg(in_.only));
    setSizeGripEnabled(true);
    Frontend& fe = *in_.fe;

    // the top bar: MAME, the versions, the filters, the search
    ui_->SystemIcon->setPixmap(QIcon(fe.mainIcon("emu_mame.ico")).pixmap(64, 64));
    ui_->LabelEmulatorVersion->setText(in_.emulatorVersion);
    ui_->LabelGamesListVersion->setText(tr("Games List: %1").arg(in_.listVersion));
    ui_->MAMEMachinesFilterIcon->setPixmap(QIcon(fe.mainIcon("media_cartridge.ico")).pixmap(16, 16));
    ui_->ButtonFilterTitleApply_ToolBar->setIcon(QIcon(fe.mainIcon("search.ico")));
    ui_->ButtonFilterTitleApply_ToolBar->setToolTip(tr("Search Game"));
    notFound_ = new QCheckBox(tr("Sets Not Found"), ui_->TopBar);
    notFound_->setObjectName("SetsFilter_NotFound");
    notFound_->setToolTip(tr("Show the sets with no file at all (the games list shows them as missing already)"));
    notFound_->setFont(ui_->SetsFilter_BiosROMs->font());
    bool software = false;
    for (auto it = in_.results.cbegin(); it != in_.results.cend(); ++it) software = software || it.key().contains(':');
    ui_->MAMEMachinesFilter->setVisible(software);
    ui_->MAMEMachinesFilterIcon->setVisible(software);
    ui_->SearchBarEdit->setPlaceholderText(tr("Click here to search"));

    // the list, in place of the form's
    auto* tree = new ResultsTree(this);
    tree_ = tree;
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setIconSize(QSize(16, 16));
    tree_->setHeaderLabels({QStringLiteral("      ") + tr("Name"), tr("CRC32"), tr("SHA-1"), tr("Size"), tr("BIOS/Device"), tr("Status")});
    {   // the checksums whole (Consolas), the name gets the rest
        QFont f = tree_->font();
        f.setFamily(QStringLiteral("Consolas"));
        const QFontMetrics mono(f);
        const int crc = mono.horizontalAdvance(QString(8, '0')) + 18, sha1 = mono.horizontalAdvance(QString(40, '0')) + 18;
        const int size = 80, device = 90, status = 150;
        tree_->header()->resizeSection(CCrc, crc);
        tree_->header()->resizeSection(CSha1, sha1);
        tree_->header()->resizeSection(CSize, size);
        tree_->header()->resizeSection(CDevice, device);
        tree_->header()->resizeSection(CName, std::max(220, 999 - crc - sha1 - size - device - status - 24)); // (the design's width)
    }
    tree_->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    tree->onEnter = [this] { selectCurrent(); };
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this] { selectCurrent(); });
    // the open / close all button in the list's corner, over the Name title's space
    ui_->ButtonToggleTree->setParent(tree_);
    ui_->ButtonToggleTree->setGeometry(tree_->frameWidth(), tree_->frameWidth(), ui_->ButtonToggleTree->width(),
                                       tree_->header()->sizeHint().height());
    ui_->ButtonToggleTree->show();
    connect(ui_->ButtonToggleTree, &QPushButton::clicked, this, [this] {
        bool open = false;
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) open = open || tree_->topLevelItem(i)->isExpanded();
        open ? tree_->collapseAll() : tree_->expandAll();
    });
    layoutTop(software);

    for (QCheckBox* c : {ui_->SetsFilter_CHDs, ui_->SetsFilter_DeviceROMs, ui_->SetsFilter_BiosROMs, notFound_})
        connect(c, &QCheckBox::toggled, this, [this] { filter(); });
    connect(ui_->MAMEMachinesFilter, &QComboBox::currentIndexChanged, this, [this] { filter(); });
    connect(ui_->ButtonFilterTitleApply_ToolBar, &QPushButton::clicked, this, [this] { search(ui_->SearchBarEdit->text()); });
    connect(ui_->SearchBarEdit, &QLineEdit::returnPressed, this, [this] { search(ui_->SearchBarEdit->text()); });
    ui_->SearchBarEdit->installEventFilter(this);

    menu_ = new QMenu(this);
    QAction* sel = menu_->addAction(tr("Select Game in Games List"), this, &ScanResultsDialog::selectCurrent);
    sel->setShortcut(Qt::Key_Return);
    sel->setShortcutContext(Qt::WidgetShortcut); // shown only; the list handles the key
    menu_->addAction(tr("Open/Close All"), ui_->ButtonToggleTree, &QPushButton::click);
    copyCommand_ = menu_->addAction(tr("Copy chdman Command"), this, [this] {
        if (QTreeWidgetItem* it = tree_->currentItem()) QGuiApplication::clipboard()->setText(it->data(CName, CommandRole).toString());
    });
    copyCommand_->setObjectName("CopyChdmanCommand");
    menu_->addSeparator();
    menu_->addAction(tr("Save List To File"), this, &ScanResultsDialog::saveListAsk);

    fill();
    // the BIOS/device and status columns as wide as their texts, the name gets what's left
    {
        const int before = tree_->header()->sectionSize(CDevice) + tree_->header()->sectionSize(CStatus);
        for (int c : {CDevice, CStatus}) {
            int want = tree_->header()->sectionSizeHint(c);
            const QFontMetrics fm(tree_->font());
            for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
                const QTreeWidgetItem* t = tree_->topLevelItem(i);
                want = std::max(want, fm.horizontalAdvance(t->text(c)) + 12);
                for (int k = 0; k < t->childCount(); ++k) want = std::max(want, fm.horizontalAdvance(t->child(k)->text(c)) + 12);
            }
            tree_->header()->resizeSection(c, std::max(tree_->header()->sectionSize(c), want));
        }
        const int grew = tree_->header()->sectionSize(CDevice) + tree_->header()->sectionSize(CStatus) - before;
        tree_->header()->resizeSection(CName, std::max(220, tree_->header()->sectionSize(CName) - grew));
    }
    // a single set: open, with nothing hidden
    if (!in_.only.isEmpty()) {
        for (QCheckBox* c : {ui_->SetsFilter_DeviceROMs, ui_->SetsFilter_BiosROMs, notFound_}) c->setChecked(true);
        tree_->expandAll();
    }
    filter();
    // only sets not found (nothing incomplete): show those rather than an empty list
    if (in_.showNotFound || (shown().isEmpty() && tree_->topLevelItemCount() > 0)) notFound_->setChecked(true);
    if (in_.only.isEmpty() && shown().size() <= 100) tree_->expandAll(); // a short list open: what's missing at once
}

ScanResultsDialog::~ScanResultsDialog() = default;

void ScanResultsDialog::fill() {
    tree_->clear();
    const bool dark = palette().color(QPalette::Base).lightness() < 128;
    const QColor red = dark ? QColor(0xff, 0x80, 0x80) : QColor(Qt::red);
    QFont mono = tree_->font();
    mono.setFamily(QStringLiteral("Consolas"));
    const QLocale loc;
    Frontend& fe = *in_.fe;
    const QIcon notFoundIcon(fe.mainIcon("scanresult_notfound.ico")), badIcon(fe.mainIcon("scanresult_badcrc.ico")),
        missIcon(fe.mainIcon("scanresult_missroms.ico")), chdIcon(fe.mainIcon("chd.ico"));
    QList<QTreeWidgetItem*> items;
    for (auto it = in_.results.cbegin(); it != in_.results.cend(); ++it) {
        RomAudit::Set s = it.value();
        if (!in_.only.isEmpty() && s.key != in_.only) continue;
        if (!in_.showSamples) { // Audio Samples unticked in Scan Games: an earlier scan's samples left out
            s.samples = RomAudit::NotChecked;
            s.files.erase(std::remove_if(s.files.begin(), s.files.end(), [](const RomAudit::File& f) { return f.kind == RomAudit::Sample; }),
                          s.files.end());
        }
        const bool romsBad = s.roms == RomAudit::Bad || s.roms == RomAudit::SetNotFound;
        const bool samplesBad = s.samples == RomAudit::Bad || s.samples == RomAudit::SetNotFound;
        if (!romsBad && !samplesBad) continue;
        const MameGame* g = in_.game ? in_.game(s.key) : nullptr;
        auto* top = new QTreeWidgetItem;
        const QString title = g ? g->title : QString();
        top->setText(CName, title.isEmpty() ? s.key : QStringLiteral("%1 [%2]").arg(title, s.key));
        top->setData(CName, KeyRole, s.key);
        top->setToolTip(CName, top->text(CName));
        if (g && in_.icon) top->setIcon(CName, in_.icon(*g));
        QStringList what;
        if (s.roms == RomAudit::SetNotFound) what << tr("Not found");
        else if (s.roms == RomAudit::Bad) what << tr("Incomplete");
        if (s.samples == RomAudit::SetNotFound) what << tr("Samples not found");
        else if (s.samples == RomAudit::Bad) what << tr("Samples missing");
        top->setText(CStatus, what.join(QStringLiteral(", ")));
        top->setForeground(CStatus, red);
        if (!s.parent.isEmpty()) top->setText(CDevice, s.parent), top->setToolTip(CDevice, tr("Clone of %1").arg(s.parent));
        top->setData(CName, NotFoundRole, s.roms == RomAudit::SetNotFound && !samplesBad);
        top->setData(CName, ChdRole, s.hasDisks() || (s.roms == RomAudit::SetNotFound && g && g->mediaType == 1));
        top->setData(CName, BiosRole, g && !g->bios.isEmpty());
        top->setData(CName, DeviceRole, g && g->romFlag(MameGame::DeviceRoms));
        top->setData(CName, SoftwareRole, s.key.contains(':'));
        // the original's order: device sets, BIOS sets, then the games by title
        const int rank = g && g->kind == SetKind::Device ? 0 : g && g->kind == SetKind::Bios ? 1 : 2;
        top->setData(CName, SortRole, QString::number(rank) + (title.isEmpty() ? s.key : title).toLower() + '\x01' + s.key);
        // a set not found: its file, where MAME looks for it ("pacman.zip", "nes\\smb.zip", and the
        // CHD folder for a game with one)
        if (s.roms == RomAudit::SetNotFound) {
            const QString base = s.key.contains(':') ? QString(s.key).replace(':', '\\') : s.key;
            QStringList what{tr("%1.zip (or .7z)").arg(base)};
            if (g && g->mediaType == 1) what << tr("%1\\ (the CHD folder)").arg(base);
            for (const QString& w : what) {
                auto* c = new QTreeWidgetItem(top);
                c->setText(CName, w);
                c->setFont(CName, mono);
                c->setIcon(CName, w == what.value(1) ? chdIcon : notFoundIcon);
                c->setText(CStatus, tr("Not found"));
                c->setForeground(CStatus, red);
            }
        }
        for (const RomAudit::File& f : s.files) {
            auto* c = new QTreeWidgetItem(top);
            c->setText(CName, f.kind == RomAudit::Sample ? f.name + ".wav" : f.name);
            c->setFont(CName, mono);
            c->setIcon(CName, f.kind == RomAudit::Disk ? chdIcon : f.kind == RomAudit::Sample ? missIcon
                                                       : f.problem == RomAudit::NotFound ? notFoundIcon : badIcon);
            const QString crc = hashPart(f.expected, "CRC"), sha1 = hashPart(f.expected, "SHA1");
            c->setText(CCrc, crc);
            c->setText(CSha1, sha1);
            c->setFont(CCrc, mono);
            c->setFont(CSha1, mono);
            if (!f.found.isEmpty()) {
                const QString found = tr("Found: %1").arg(f.found);
                c->setToolTip(CCrc, found);
                c->setToolTip(CSha1, found);
                c->setToolTip(CStatus, found);
            }
            if (f.size >= 0) c->setText(CSize, loc.toString(f.size));
            c->setTextAlignment(CSize, Qt::AlignRight | Qt::AlignVCenter);
            c->setText(CDevice, f.device);
            c->setText(CStatus, problemText(f));
            c->setForeground(CStatus, red);
            // a CHD that's there, but MAME can't use (ChdCheck): why, and the fix
            if (f.chdIssue != RomAudit::File::NoIssue) {
                const QString file = QDir::toNativeSeparators(f.chdFile), parent = QDir::toNativeSeparators(f.chdParent);
                QString status, why, command;
                switch (f.chdIssue) {
                case RomAudit::File::DeltaParentElsewhere:
                    status = tr("Delta CHD: its parent is another set's");
                    command = ChdCheck::copyCommand(f.chdFile, f.chdParent);
                    why = tr("%1 is there, but it's a delta CHD: it holds only what differs from %2. "
                             "MAME looks for that one among the CHDs of this set's parents only, so it can't open it.")
                              .arg(file, parent) +
                          "\n\n" + tr("Make a stand-alone copy, then put it in place of the old file:") + "\n" + command;
                    break;
                case RomAudit::File::DeltaParentMissing:
                    status = tr("Delta CHD: its parent isn't found");
                    why = tr("%1 is there, but it's a delta CHD: MAME needs the CHD it was made from (SHA-1 %2), "
                             "which isn't in the ROM folders.")
                              .arg(file, f.chdParentSha1);
                    break;
                default:
                    status = tr("Another CHD than MAME's");
                    why = tr("%1 is there, but it isn't the CHD this version of MAME wants (another SHA-1). "
                             "chdman info -i shows its SHA-1; mame -listxml shows the one MAME wants.")
                              .arg(file);
                    break;
                }
                c->setText(CStatus, status);
                for (int col = 0; col < ColCount; ++col) c->setToolTip(col, why);
                c->setData(CName, CommandRole, command);
                c->setData(CName, WhyRole, why);
            }
        }
        items << top;
    }
    std::sort(items.begin(), items.end(), [](QTreeWidgetItem* a, QTreeWidgetItem* b) {
        return a->data(CName, SortRole).toString() < b->data(CName, SortRole).toString();
    });
    tree_->addTopLevelItems(items);
}

// FilterGamesList, SearchGame
void ScanResultsDialog::filter() {
    const int kind = ui_->MAMEMachinesFilter->isVisible() ? ui_->MAMEMachinesFilter->currentIndex() : 0;
    const QString text = searchText_.trimmed();
    int shownCount = 0;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = tree_->topLevelItem(i);
        bool show = true;
        const bool soft = it->data(CName, SoftwareRole).toBool();
        if (kind == 1) show = !soft;
        if (kind == 2) show = soft;
        if (show && ui_->SetsFilter_CHDs->isChecked()) show = it->data(CName, ChdRole).toBool(); // takes priority, as the original
        if (show && it->data(CName, BiosRole).toBool()) show = ui_->SetsFilter_BiosROMs->isChecked();
        if (show && it->data(CName, DeviceRole).toBool()) show = ui_->SetsFilter_DeviceROMs->isChecked();
        if (show && it->data(CName, NotFoundRole).toBool()) show = notFound_->isChecked();
        if (show && !text.isEmpty()) show = it->text(CName).contains(text, Qt::CaseInsensitive);
        it->setHidden(!show);
        shownCount += show;
    }
    ui_->LabelTotalGames->setText(shownCount == 1 ? tr("1 Game") : tr("%1 Games").arg(QLocale().toString(shownCount)));
}

// The top bar in layouts: MAME's picture; the versions; the count, the filters (the software list
// one when there are software list games) and the search at the right. The list under it.
void ScanResultsDialog::layoutTop(bool software) {
    using vcl::boxed, vcl::loose;
    QWidget* top = ui_->TopBar;
    auto* h = boxed(new QHBoxLayout(top), 4, 4, 8, 4, 7);
    h->addWidget(ui_->SystemIcon, 0, Qt::AlignTop);
    auto* v = boxed(new QVBoxLayout, 0, 0, 0, 0, 3);
    for (QLabel* l : {(QLabel*)ui_->LabelEmulatorVersion, (QLabel*)ui_->LabelGamesListVersion, (QLabel*)ui_->LabelTotalGames,
                      (QLabel*)ui_->LabelSearchBar})
        loose(l);
    v->addWidget(ui_->LabelEmulatorVersion);
    v->addWidget(ui_->LabelGamesListVersion);
    v->addStretch();
    auto* row = boxed(new QHBoxLayout, 0, 0, 0, 0, 10);
    row->addWidget(ui_->LabelTotalGames);
    row->addSpacing(20);
    if (software) {
        auto* m = boxed(new QHBoxLayout, 0, 0, 0, 0, 3);
        m->addWidget(ui_->MAMEMachinesFilterIcon);
        loose(ui_->MAMEMachinesFilter);
        m->addWidget(ui_->MAMEMachinesFilter);
        row->addLayout(m);
    }
    for (QCheckBox* c : {ui_->SetsFilter_CHDs, ui_->SetsFilter_DeviceROMs, ui_->SetsFilter_BiosROMs, notFound_}) {
        loose(c);
        row->addWidget(c);
    }
    row->addStretch();
    row->addWidget(ui_->LabelSearchBar);
    loose(ui_->SearchBarEdit);
    ui_->SearchBarEdit->setMinimumWidth(150);
    row->addWidget(ui_->SearchBarEdit);
    row->addWidget(ui_->ButtonFilterTitleApply_ToolBar);
    v->addLayout(row);
    h->addLayout(v, 1);
    top->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* root = boxed(new QVBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(top);
    root->addWidget(tree_, 1);
    for (QWidget* x : {(QWidget*)this, top}) x->setProperty("vclLaidOut", true);
}

void ScanResultsDialog::search(const QString& text) {
    searchText_ = text;
    filter();
}

bool ScanResultsDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == ui_->SearchBarEdit && e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape &&
        !ui_->SearchBarEdit->text().isEmpty()) {
        ui_->SearchBarEdit->clear(); // Esc clears the search first
        search({});
        return true;
    }
    return QDialog::eventFilter(o, e);
}

void ScanResultsDialog::contextMenuEvent(QContextMenuEvent* e) {
    if (!tree_->geometry().contains(e->pos())) return;
    // Copy chdman Command: for a delta CHD whose parent is found
    QTreeWidgetItem* it = tree_->currentItem();
    copyCommand_->setVisible(it && !it->data(CName, CommandRole).toString().isEmpty());
    menu_->exec(e->globalPos());
}

void ScanResultsDialog::selectCurrent() {
    QTreeWidgetItem* it = tree_->currentItem();
    if (!it) return;
    while (it->parent()) it = it->parent();
    const QString key = it->data(CName, KeyRole).toString();
    if (in_.select && !in_.select(key))
        QMessageBox::information(this, windowTitle(), tr("%1 isn't shown in the games list: the tool bar filters or the search hide it.").arg(it->text(CName)));
}

QStringList ScanResultsDialog::shown() const {
    QStringList out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        if (!tree_->topLevelItem(i)->isHidden()) out << tree_->topLevelItem(i)->data(CName, KeyRole).toString();
    return out;
}

QString ScanResultsDialog::totalText() const { return ui_->LabelTotalGames->text(); }
QString ScanResultsDialog::commandOf(int top, int child) const {
    const QTreeWidgetItem* t = tree_->topLevelItem(top);
    return t && t->child(child) ? t->child(child)->data(CName, CommandRole).toString() : QString();
}
void ScanResultsDialog::setNotFoundShown(bool on) { notFound_->setChecked(on); }
void ScanResultsDialog::setChdsOnly(bool on) { ui_->SetsFilter_CHDs->setChecked(on); }
void ScanResultsDialog::setKindFilter(int index) { ui_->MAMEMachinesFilter->setCurrentIndex(index); }

// The sets shown, each with its files, as a text file
bool ScanResultsDialog::saveList(const QString& file) const {
    QString out;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = tree_->topLevelItem(i);
        if (it->isHidden()) continue;
        out += it->text(CName) + " - " + it->text(CStatus) + "\r\n";
        for (int c = 0; c < it->childCount(); ++c) {
            const QTreeWidgetItem* f = it->child(c);
            QString line = "    " + f->text(CName);
            if (!f->text(CSize).isEmpty()) line += " (" + f->text(CSize) + ")";
            if (!f->text(CDevice).isEmpty()) line += " [" + f->text(CDevice) + "]";
            line += " - " + f->text(CStatus);
            if (!f->text(CCrc).isEmpty()) line += "  CRC(" + f->text(CCrc) + ")";
            if (!f->text(CSha1).isEmpty()) line += " SHA1(" + f->text(CSha1) + ")";
            out += line + "\r\n";
            if (const QString why = f->data(CName, WhyRole).toString(); !why.isEmpty())
                out += "        " + QString(why).replace("\n\n", "\r\n        ").replace("\n", "\r\n        ") + "\r\n";
        }
    }
    return writeTextFile(file, out, true);
}

void ScanResultsDialog::saveListAsk() {
    const QString start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath("missing_roms.txt");
    const QString file = QFileDialog::getSaveFileName(this, tr("Save the List to a Text File"), start, tr("Text files (*.txt)"));
    if (file.isEmpty()) return;
    if (!saveList(file)) FileProblem::cantSave(this, windowTitle(), file);
}

} // namespace eld
