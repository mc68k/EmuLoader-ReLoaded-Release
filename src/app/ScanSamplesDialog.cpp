#include "ScanSamplesDialog.h"
#include "FileProblem.h"

#include "ElDatabase.h"
#include "GamesModel.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "ui_FormArcadeScanAudioSamples.h"

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSet>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>

namespace eld {

namespace {
enum Col { CTitle, CName, CClone, CDriver, CSample, ColCount };
enum Role { NameRole = Qt::UserRole, SetStatusRole };

// Enter plays, Del removes (ScanMissAudioFilesListViewKeyAction)
class SamplesTree : public QTreeWidget {
public:
    std::function<void()> onEnter, onDelete;
    using QTreeWidget::QTreeWidget;
protected:
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && onEnter) { onEnter(); return; }
        if (e->key() == Qt::Key_Delete && onDelete) { onDelete(); return; }
        QTreeWidget::keyPressEvent(e);
    }
};
} // namespace

ScanSamplesDialog::ScanSamplesDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormArcadeScanAudioSamples), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Scan Missing Audio Samples"));
    setSizeGripEnabled(true);

    auto* tree = new SamplesTree(this);
    list_ = tree;
    vcl::replaceDesigned(ui_->ScanMissAudioFilesListView, list_); // (in the form's place)
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setSortingEnabled(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setIconSize(QSize(16, 16));
    list_->setHeaderLabels({GamesModel::columnTitle(GamesModel::Title), GamesModel::columnTitle(GamesModel::Name),
                            GamesModel::columnTitle(GamesModel::Clone), GamesModel::columnTitle(GamesModel::DriverStatus),
                            tr("Samples Set")});
    list_->header()->resizeSection(CTitle, 420);
    list_->header()->resizeSection(CName, 100);
    list_->header()->resizeSection(CClone, 100);
    list_->header()->resizeSection(CDriver, 110);
    list_->sortByColumn(CTitle, Qt::AscendingOrder);
    tree->onEnter = [this] { playSelected(); };
    tree->onDelete = [this] { removeSelected(); };
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { playSelected(); });
    vcl::relayout(this);

    // the list's menu (PopupAudioSamples)
    menu_ = new QMenu(this);
    QAction* play = menu_->addAction(tr("Play"), this, &ScanSamplesDialog::playSelected);
    play->setShortcut(Qt::Key_Return);
    play->setShortcutContext(Qt::WidgetShortcut); // shown only; the list handles the key
    menu_->addSeparator();
    QAction* remove = menu_->addAction(tr("Remove Selected"), this, &ScanSamplesDialog::removeSelected);
    remove->setShortcut(Qt::Key_Delete);
    remove->setShortcutContext(Qt::WidgetShortcut);
    menu_->addAction(tr("Clear List"), this, [this] { list_->clear(); updateTotal(); });
    menu_->addAction(tr("Rebuild List"), this, [this] { scan(); });
    menu_->addSeparator();
    auto* group = new QActionGroup(this);
    const QPair<QString, Show> shows[] = {{tr("Show All Games"), ShowAll}, {tr("Show Available Games Only"), ShowHave},
                                          {tr("Show Missing ROMs/CHDs Only"), ShowMiss}};
    for (const auto& [text, s] : shows) {
        QAction* a = menu_->addAction(text);
        a->setCheckable(true);
        a->setChecked(s == show_);
        a->setData(int(s));
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, s = s] { setShow(s); });
    }
    menu_->addSeparator();
    menu_->addAction(tr("Save List To File"), this, &ScanSamplesDialog::saveListAsk);

    // the download link (LabelDownloadLinkClick)
    ui_->LabelDownloadLink->setCursor(Qt::PointingHandCursor);
    ui_->LabelDownloadLink->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(ui_->LabelDownloadLink, &QLabel::linkActivated, this, [](const QString& u) { QDesktopServices::openUrl(QUrl(u)); });
    ui_->LabelDownloadLink->setTextFormat(Qt::RichText);
    ui_->LabelDownloadLink->setText(QString("<a href=\"https://www.progettosnaps.net/samples/\" style=\"color:#000080\">%1</a>")
                                        .arg(tr("Click here to download samples (Progetto-SNAPS)").toHtmlEscaped()));
    ui_->LabelDownloadLink->setToolTip("https://www.progettosnaps.net/samples/");
    ui_->LabelDownloadLink->adjustSize();
    vcl::layoutForm(this); // every text whole in any language and size
}

ScanSamplesDialog::~ScanSamplesDialog() = default;

void ScanSamplesDialog::contextMenuEvent(QContextMenuEvent* e) {
    if (list_->geometry().contains(e->pos())) menu_->exec(e->globalPos());
}

QString ScanSamplesDialog::samplesFoldersText() const {
    QStringList f;
    for (const QString& d : folders_) f << QDir::toNativeSeparators(d);
    return f.join('\n');
}

// AddGamesToList
bool ScanSamplesDialog::scan(bool quiet) {
    list_->clear();
    message_.clear();
    auto say = [&](const QString& m) {
        message_ = m;
        if (!quiet) QMessageBox::information(parentWidget() ? parentWidget() : this, windowTitle(), m);
        return false;
    };
    const QString section = Frontend::section(in_.system);
    const QString samplesFile = in_.fe->samplesFile(in_.system);
    const QHash<QString, QString> sets = ElDatabase::loadSamples(samplesFile);
    if (sets.isEmpty())
        return say(tr("The games list doesn't say which games play audio samples (%1 is missing).\n\n"
                      "Create the games list again (Menu Options > MAME/Arcade > Create Arcade Games List).")
                       .arg(QDir::toNativeSeparators(samplesFile)));
    // the samples files: <set>.zip / <set>.7z in the samplepath folders (GetSampleFiles)
    folders_ = in_.fe->sampleFolders(in_.system);
    QSet<QString> files;
    for (const QString& d : folders_)
        for (const QString& f : QDir(d).entryList({"*.zip", "*.7z"}, QDir::Files)) files.insert(f.toLower());
    if (files.isEmpty())
        return say(tr("No samples files were found for %1: all its games are missing their samples. EmuLoader only "
                      "supports zipped / 7-zipped samples sets (not .wav or .flac files).\n\n%1 samples folders:\n%2")
                       .arg(section, samplesFoldersText()));
    // the available games (and those missing ROMs) that need samples, once per samples set
    QSet<QString> added;
    list_->setSortingEnabled(false);
    for (int r = 0; r < in_.model->rowCount(); ++r) {
        const MameGame& g = in_.model->game(r);
        if (g.audioType != 2 || !g.have()) continue;
        const QString set = sets.value(g.name, g.name);
        if (files.contains(set.toLower() + ".zip") || files.contains(set.toLower() + ".7z")) continue;
        if (added.contains(set)) continue;
        added.insert(set);
        auto* it = new QTreeWidgetItem(list_);
        it->setText(CTitle, in_.model->title(g));
        it->setIcon(CTitle, in_.model->icon(g));
        it->setText(CName, g.name);
        it->setText(CClone, g.cloneOf);
        it->setText(CDriver, in_.model->data(in_.model->index(r, GamesModel::DriverStatus), Qt::DisplayRole).toString());
        it->setText(CSample, set);
        it->setData(CTitle, NameRole, g.name);
        it->setData(CTitle, SetStatusRole, int(g.setStatus));
        if (g.setStatus == 1) for (int c = 0; c < ColCount; ++c) it->setForeground(c, QColor(0xc0, 0, 0)); // missing ROMs: red
    }
    list_->setSortingEnabled(true);
    if (list_->topLevelItemCount() == 0)
        return say(tr("Found the samples of all the available games."));
    filter();
    return true;
}

void ScanSamplesDialog::setShow(Show s) {
    show_ = s;
    for (QAction* a : menu_->actions())
        if (a->isCheckable()) a->setChecked(a->data().toInt() == int(s));
    filter();
}

// SetFilter
void ScanSamplesDialog::filter() {
    QTreeWidgetItem* first = nullptr;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* it = list_->topLevelItem(i);
        const int st = it->data(CTitle, SetStatusRole).toInt();
        const bool shown = show_ == ShowAll || (show_ == ShowHave ? st == 0 : st == 1);
        it->setHidden(!shown);
        if (shown && !first) first = it;
    }
    QTreeWidgetItem* cur = list_->currentItem();
    if ((!cur || cur->isHidden()) && first) list_->setCurrentItem(first);
    updateTotal();
}

int ScanSamplesDialog::missingCount() const {
    int n = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) n += !list_->topLevelItem(i)->isHidden();
    return n;
}

QStringList ScanSamplesDialog::listedNames() const {
    QStringList out;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (!list_->topLevelItem(i)->isHidden()) out << list_->topLevelItem(i)->text(CName);
    return out;
}

void ScanSamplesDialog::updateTotal() {
    ui_->LabelTotalItems->setText(tr("%n Missing Samples", nullptr, missingCount()));
    ui_->LabelTotalItems->adjustSize();
}

// PopupPlayGameClick
void ScanSamplesDialog::playSelected() {
    const QList<QTreeWidgetItem*> sel = list_->selectedItems();
    if (sel.size() != 1 || !in_.play) return;
    in_.play(sel.first()->data(CTitle, NameRole).toString());
    list_->setFocus();
}

void ScanSamplesDialog::removeSelected() {
    for (QTreeWidgetItem* it : list_->selectedItems()) delete it;
    updateTotal();
    list_->setFocus();
}

// MenuSaveListToFileClick
bool ScanSamplesDialog::saveList(const QString& file) const {
    const QString section = Frontend::section(in_.system);
    QString out = QString("----------> %1 Audio Samples <----------\r\n\r\n").arg(section);
    QStringList f;
    for (const QString& d : folders_) f << QDir::toNativeSeparators(d);
    out += " -> Samples Folder: " + f.join(';') + "\r\n";
    out += "\r\n\r\n -> " + tr("%n Missing Samples", nullptr, missingCount()) + "\r\n";
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = list_->topLevelItem(i);
        if (it->isHidden()) continue;
        out += "\r\n > " + it->text(CTitle) + "\r\n";
        out += it->text(CClone).isEmpty()
                   ? QString("name: %1, sample name: %2 [Missing]\r\n").arg(it->text(CName), it->text(CSample))
                   : QString("name: %1 [clone of: %2], sample name: %3 [Missing]\r\n")
                         .arg(it->text(CName), it->text(CClone), it->text(CSample));
    }
    return writeTextFile(file, out, true);
}

void ScanSamplesDialog::saveListAsk() {
    if (missingCount() == 0) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString file = QFileDialog::getSaveFileName(this, tr("Save Audio Samples List to a Text File"),
                                                      QDir(dir).filePath(Frontend::fileBase(in_.system) + "_missing_samples.txt"),
                                                      tr("Text files (*.txt);;All files (*.*)"));
    if (file.isEmpty()) return;
    if (saveList(file))
        QMessageBox::information(this, windowTitle(), tr("The file \"%1\" was made from the current list.").arg(QDir::toNativeSeparators(file)));
    else
        FileProblem::cantSave(this, windowTitle(), file);
}

} // namespace eld
