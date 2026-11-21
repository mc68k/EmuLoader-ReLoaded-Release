#include "CategorySettingsDialog.h"

#include "ElWidgets.h"
#include "ImageFinder.h"
#include "MameIni.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "WinIni.h"
#include "SettingsLayout.h"
#include "ui_FormImageCategorySettings.h"

#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMouseEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QTimer>

namespace eld {

namespace {
QString sectionOf(SystemId s) { return s == SystemId::HBMAME ? QStringLiteral("HBMAME") : QStringLiteral("MAME"); }
} // namespace

CategorySettingsDialog::CategorySettingsDialog(Frontend& fe, SystemId current, int startCategory, const Values& values,
                                               std::function<QIcon(int)> categoryIcon, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormImageCategorySettings), fe_(fe), v_(values), saved_(values),
      icon_(std::move(categoryIcon)) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Image Category Settings"));

    // Systems (MAME, HBMAME): the folders are per system; the rest is the same for all.
    systemsView_ = new QListWidget(this);
    systemsView_->setViewMode(QListView::IconMode);
    systemsView_->setIconSize(QSize(48, 48));
    systemsView_->setGridSize(QSize(110, 82));
    systemsView_->setMovement(QListView::Static);
    systemsView_->setFlow(QListView::LeftToRight);
    systemsView_->setFrameShape(QFrame::NoFrame);
    systemsView_->setFont(QFont("Verdana", 8));
    int start = 0;
    for (SystemId s : {SystemId::MAME, SystemId::HBMAME}) {
        QString res = fe_.mainIcon(s == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico");
        auto* it = new QListWidgetItem(QIcon(res), sectionOf(s), systemsView_);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        bool available = !fe_.emulatorFile(s).isEmpty();
        if (!available) it->setForeground(Qt::gray); // ghosted: no emulator set
        it->setData(Qt::UserRole, available);
        if (s == current) start = int(systems_.size());
        systems_ << s;
        QStringList f = ImageFinder::readSettings(fe_.dataDir(), fe_.userDir(), fe_.mameFolder(s), fe_.mameIniPath(s), sectionOf(s));
        folders_ << f;
        original_ << f;
    }

    // Category tiles with a check box each (ImageCategory_Selector)
    categories_ = new QListWidget(this);
    categories_->setViewMode(QListView::IconMode);
    categories_->setIconSize(QSize(48, 48));
    categories_->setGridSize(QSize(68, 108));
    categories_->setMovement(QListView::Static);
    categories_->setFlow(QListView::LeftToRight);
    categories_->setWrapping(true);
    categories_->setResizeMode(QListView::Adjust);
    categories_->setWordWrap(true);
    categories_->setFrameShape(QFrame::NoFrame);
    categories_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 9 + 8 tiles, as the original
    categories_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categories_->setFont(QFont("Verdana", 7));
    for (int c = 0; c < kCount; ++c) {
        auto* it = new QListWidgetItem(icon_ ? icon_(c) : QIcon(), ImageFinder::title(c), categories_);
        it->setData(Qt::UserRole, c);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(c == 1 || v_.enabled[c] ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(c == 1 ? tr("In-Game Snapshot can't be hidden") : tr("Show this category with the image buttons"));
        if (c == 1) { // Video, after the snapshot (as the categories go round)
            auto* v = new QListWidgetItem(icon_ ? icon_(kVideo) : QIcon(), tr("Video"), categories_);
            v->setData(Qt::UserRole, kVideo);
            v->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            v->setFlags(v->flags() | Qt::ItemIsUserCheckable);
            v->setCheckState(v_.video ? Qt::Checked : Qt::Unchecked);
            v->setToolTip(tr("The game's video in the picture panel (while video previews play in EmuLoader: Settings > Video)"));
            if (!v_.videoOff.isEmpty()) { // left out now: grayed, its tick kept for when it's back
                v->setFlags(v->flags() & ~Qt::ItemIsUserCheckable);
                v->setForeground(QBrush(Qt::gray));
                v->setToolTip(v_.videoOff);
            }
        }
    }
    connect(categories_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        const int c = it->data(Qt::UserRole).toInt();
        if (c == 1 && it->checkState() != Qt::Checked) { // ImageCategory_SelectorItemCheckChange
            QSignalBlocker b(categories_);
            it->setCheckState(Qt::Checked);
        }
        const bool on = it->checkState() == Qt::Checked;
        if (c == kVideo) v_.video = on;
        else v_.enabled[c] = on;
        it->setForeground(on && !(c == kVideo && !v_.videoOff.isEmpty()) ? QBrush() : QBrush(Qt::gray));
    });

    // Texts
    ui_->LabelImageCategoryBackgroundColor->setText(tr("Image Background Color"));
    ui_->ImageCategoryBackgroundColor->setToolTip(tr("Image Category Background Color"));
    ui_->ButtonImageCategoryBackgroundColorReset->setText(tr("Reset"));
    ui_->ButtonImageCategoryBackgroundColorReset->setToolTip(tr("Reset color to current"));
    ui_->ImageSingleBackgroundColorEnabled->setText(tr("Single Background Color"));
    ui_->ImageSingleBackgroundColorEnabled->setToolTip(tr("Use the same background color behind every picture category, instead of a color for each one."));
    ui_->ImageSingleBackgroundColor->setToolTip(tr("Image Single Background Color"));
    ui_->ImageSingleBackgroundColorButtonReset->setText(tr("Reset"));
    ui_->ImageSingleBackgroundColorButtonReset->setToolTip(tr("Reset color to current"));
    ui_->ImageCategoryFolder->setToolTip(tr("Several folders can be given for MAME and HBMAME, separated by ;. "
                                            "A relative folder is relative to the emulator's folder."));
    ui_->ButtonSelectImageCategoryFolder->setText(tr("Select"));
    ui_->ButtonSelectImageCategoryFolder->setToolTip(tr("Click here to select a folder"));
    ui_->ButtonClearImageCategoryFolder->setText(tr("Clear"));
    ui_->ButtonClearImageCategoryFolder->setToolTip(tr("Empty the folder box (MAME's default folder is used)"));
    ui_->ButtonResetImageCategoryFolder->setText(tr("Reset"));
    ui_->ButtonResetImageCategoryFolder->setToolTip(tr("Reset folder to current"));
    ui_->ButtonDefaultImageCategoryFolder->setText(tr("Default"));
    ui_->ButtonDefaultImageCategoryFolder->setToolTip(tr("Use the default folder name"));
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    ui_->ImageSingleBackgroundColorEnabled->adjustSize();
    ui_->LabelImageCategoryBackgroundColor->adjustSize();

    // Colors
    ui_->ImageSingleBackgroundColorEnabled->setChecked(v_.single);
    ui_->ImageSingleBackgroundColor->setTColor(v_.singleColor);
    auto singleOn = [this] {
        bool on = ui_->ImageSingleBackgroundColorEnabled->isChecked();
        ui_->ImageSingleBackgroundColor->setEnabled(on);
        ui_->ImageSingleBackgroundColorButtonReset->setEnabled(on);
        v_.single = on;
    };
    singleOn();
    connect(ui_->ImageSingleBackgroundColorEnabled, &QCheckBox::toggled, this, singleOn);
    connect(ui_->ImageSingleBackgroundColor, &ElColorBox::colorChanged, this,
            [this] { v_.singleColor = ui_->ImageSingleBackgroundColor->tcolor(); });
    connect(ui_->ImageSingleBackgroundColorButtonReset, &QPushButton::clicked, this,
            [this] { ui_->ImageSingleBackgroundColor->setTColor(saved_.singleColor); });
    connect(ui_->ImageCategoryBackgroundColor, &ElColorBox::colorChanged, this, [this] {
        if (!filling_ && category() >= 0) v_.background[category()] = ui_->ImageCategoryBackgroundColor->tcolor();
    });
    connect(ui_->ButtonImageCategoryBackgroundColorReset, &QPushButton::clicked, this, [this] {
        if (category() >= 0) ui_->ImageCategoryBackgroundColor->setTColor(saved_.background[category()]);
    });

    // Folders
    connect(ui_->ImageCategoryFolder, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (system() >= 0 && category() >= 0) folders_[system()][category()] = t;
    });
    auto setFolder = [this](const QString& t) {
        ui_->ImageCategoryFolder->setText(t);
        if (system() >= 0 && category() >= 0) folders_[system()][category()] = t;
    };
    connect(ui_->ButtonClearImageCategoryFolder, &QPushButton::clicked, this, [setFolder] { setFolder({}); });
    connect(ui_->ButtonResetImageCategoryFolder, &QPushButton::clicked, this, [this, setFolder] {
        if (system() >= 0 && category() >= 0) setFolder(original_[system()][category()]);
    });
    connect(ui_->ButtonDefaultImageCategoryFolder, &QPushButton::clicked, this, [this, setFolder] {
        if (category() >= 0) setFolder(ImageFinder::category(category()).folder);
    });
    connect(ui_->ButtonSelectImageCategoryFolder, &QPushButton::clicked, this, [this, setFolder] {
        if (system() < 0 || category() < 0) return;
        SystemId s = systems_[system()];
        QString base = fe_.mameFolder(s);
        QStringList now = splitPathList(ui_->ImageCategoryFolder->text());
        QString start = now.isEmpty() ? base : resolvePath(base, now.first());
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select a folder for %1 [%2]").arg(ImageFinder::title(category()), sectionOf(s)), start);
        if (dir.isEmpty()) return;
        // Relative to MAME's folder when it's inside it, as MAME writes its own.
        QString rel = base.isEmpty() ? dir : QDir(base).relativeFilePath(dir);
        QString pick = !base.isEmpty() && !rel.startsWith("..") ? rel : dir;
        pick = QDir::toNativeSeparators(pick);
        // Several folders: ask whether to add it to the ones there are.
        QString cur = ui_->ImageCategoryFolder->text().trimmed();
        if (!cur.isEmpty() && !splitPathList(cur).contains(pick, Qt::CaseInsensitive) &&
            QMessageBox::question(this, windowTitle(), tr("Add this folder to the folders there are?\n\nNo replaces them.")) ==
                QMessageBox::Yes)
            pick = cur + ";" + pick;
        setFolder(pick);
    });

    connect(systemsView_, &QListWidget::currentRowChanged, this, [this] { showFolder(); });
    connect(categories_, &QListWidget::currentRowChanged, this, [this] { showCategory(); });
    systemsView_->setCurrentRow(start);
    for (int i = 0; i < categories_->count(); ++i)
        if (categories_->item(i)->data(Qt::UserRole).toInt() == std::clamp(startCategory, 0, kCount - 1)) categories_->setCurrentRow(i);
    showCategory();
    categories_->setFocus();
}

CategorySettingsDialog::~CategorySettingsDialog() = default;

int CategorySettingsDialog::system() const { return systemsView_ ? systemsView_->currentRow() : -1; }
// the selected tile's category (-1: none, or Video, which has no folder or color here)
int CategorySettingsDialog::category() const {
    QListWidgetItem* it = categories_ ? categories_->currentItem() : nullptr;
    const int c = it ? it->data(Qt::UserRole).toInt() : -1;
    return c == kVideo ? -1 : c;
}

// ImageCategory_SelectorItemSelectionChanged
void CategorySettingsDialog::showCategory() {
    int c = category();
    QListWidgetItem* cur = categories_ ? categories_->currentItem() : nullptr;
    const bool video = cur && cur->data(Qt::UserRole).toInt() == kVideo;
    if (selectedBox_) {
        for (QWidget* w : selectedBox_->findChildren<QWidget*>()) if (w != notAvailable_) w->setEnabled(!video);
        if (video) {
            selectedBox_->setTitle(tr("Video"));
            notAvailable_->setText((v_.videoOff.isEmpty() ? QString() : v_.videoOff + "\n\n") +
                                   tr("The videos' folders are set in Settings > Video."));
            notAvailable_->setStyleSheet("color: #6E6E6E;");
            notAvailable_->show();
        }
    }
    if (c < 0) return;
    filling_ = true;
    ui_->ImageCategoryBackgroundColor->setTColor(v_.background[c]);
    filling_ = false;
    ui_->LabelImageCategoryFolder->setText(ImageFinder::pngOnly(c) ? tr("Image Category Folder [%1]").arg(".png")
                                                                   : tr("Image Category Folder [%1]").arg(".png; .jpg"));
    if (selectedBox_) {
        selectedBox_->setTitle(ImageFinder::title(c));
        ui_->LabelImageCategoryFolder->setText(ImageFinder::pngOnly(c) ? tr("Folder (.png)") : tr("Folder (.png, .jpg)"));
    }
    showFolder();
}

// SystemsItemSelectionChanged / SetImageCategoryValues
void CategorySettingsDialog::showFolder() {
    int s = system(), c = category();
    if (s < 0 || c < 0) return;
    QListWidgetItem* it = systemsView_->item(s);
    bool available = it->data(Qt::UserRole).toBool();
    if (notAvailable_) {
        notAvailable_->setText(available ? QString()
                                         : tr("No emulator is set for %1, so its folders can't be changed here.").arg(sectionOf(systems_[s])));
        notAvailable_->setStyleSheet("color: #B00000;");
        notAvailable_->setVisible(!available);
    }
    ui_->ImageCategoryFolder->setText(folders_[s][c]);
    ui_->ImageCategoryFolder->setPlaceholderText(ImageFinder::category(c).folder);
    // A system without an emulator: its folders can't be changed (they're in its ini files).
    for (QWidget* w : {(QWidget*)ui_->ImageCategoryFolder, (QWidget*)ui_->ButtonSelectImageCategoryFolder,
                       (QWidget*)ui_->ButtonClearImageCategoryFolder, (QWidget*)ui_->ButtonResetImageCategoryFolder,
                       (QWidget*)ui_->ButtonDefaultImageCategoryFolder})
        w->setEnabled(available);
}

// FormCloseQuery (Apply): folders to ui.ini / mame.ini / sysimagefolders_arcade.ini; the rest is
// the caller's (image_category.ini, see save()).
void CategorySettingsDialog::accept() {
    for (int s = 0; s < systems_.size(); ++s) {
        if (folders_[s] == original_[s]) continue;
        SystemId id = systems_[s];
        if (!ImageFinder::writeSettings(fe_.dataDir(), fe_.userDir(), fe_.mameFolder(id), fe_.mameIniPath(id), sectionOf(id), folders_[s]))
            QMessageBox::warning(this, windowTitle(), tr("Could not save the %1 folders.").arg(sectionOf(id)));
        foldersChanged_ = true;
    }
    QDialog::accept();
}

void CategorySettingsDialog::save(const QString& file, const Values& v) {
    WinIni ini;
    ini.load(file);
    for (int c = 0; c < kCount; ++c) {
        QString sec = ImageFinder::category(c).section;
        ini.writeInteger("Category", sec + "_bkcolor", v.background[c]);
        if (c != 1) ini.writeInteger("Category", sec + "_enabled", v.enabled[c] ? 1 : 0);
    }
    ini.writeInteger("EmuLoaderRL", "Video_enabled", v.video ? 1 : 0); // (the original has no Video category)
    ini.writeInteger("SingleBackground", "Enabled", v.single ? 1 : 0);
    ini.writeInteger("SingleBackground", "Color", v.singleColor);
    QDir().mkpath(QFileInfo(file).absolutePath());
    ini.save(file);
}

// Show All / Hide All: every category but In-Game Snapshot on or off.
void CategorySettingsDialog::showHideAll() {
    for (int i = 0; i < categories_->count(); ++i)
        if (categories_->item(i)->data(Qt::UserRole).toInt() != 1)
            categories_->item(i)->setCheckState(showAll_ ? Qt::Checked : Qt::Unchecked);
    showAll_ = !showAll_;
    if (showHideButton_) showHideButton_->setText(showAll_ ? tr("Show All") : tr("Hide All"));
    categories_->setFocus();
}

// The Settings window's Pictures tab: the categories, the selected one's folder (per system) and
// the background colors, in groups; the Help button's text under the controls it's about.
void CategorySettingsDialog::settingsLayout() {
    using settingsui::Group;
    QLabel* like = new QLabel(this); // the explanations' gray
    like->setStyleSheet("color: #6E6E6E;");
    like->hide();
    auto hint = [like](const QString& t) { return settingsui::hint(t, like); };

    Group cats(tr("Categories"));
    categories_->setParent(nullptr);
    categories_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    categories_->setMinimumSize(6 * 68 + 6, 3 * 108 + 24); // (they wrap: as many across as fit)
    showHideButton_ = new QPushButton(showAll_ ? tr("Show All") : tr("Hide All"));
    showHideButton_->setObjectName("ShowHideAllCategories");
    showHideButton_->setAutoDefault(false);
    connect(showHideButton_, &QPushButton::clicked, this, &CategorySettingsDialog::showHideAll);
    cats.wide(categories_)
        .text(hint(tr("Ticked categories have a button above the pictures. Click a category to set its folder and "
                      "background color. In-Game Snapshot is always shown.")))
        .line({}, showHideButton_);

    ui_->ImageCategoryFolder->setMinimumWidth(ui_->ImageCategoryFolder->fontMetrics().averageCharWidth() * 24); // (a path to read)
    Group sel(tr("Selected Category"));
    selectedBox_ = sel.box();
    notAvailable_ = hint(QString());
    notAvailable_->setStyleSheet("color: #B00000;");
    sel.row(new QLabel(tr("System")), settingsui::systemSwitch(systemsView_))
        .row(ui_->LabelImageCategoryFolder, ui_->ImageCategoryFolder)
        .row(nullptr, [&] { // its buttons under it (beside it, they made the group too wide)
            auto* w = new QWidget;
            auto* h = new QHBoxLayout(w);
            h->setContentsMargins(0, 0, 0, 0);
            for (QWidget* b : {(QWidget*)ui_->ButtonSelectImageCategoryFolder, (QWidget*)ui_->ButtonClearImageCategoryFolder,
                               (QWidget*)ui_->ButtonResetImageCategoryFolder, (QWidget*)ui_->ButtonDefaultImageCategoryFolder})
                h->addWidget(b);
            h->addStretch();
            return w;
        }())
        .row(nullptr, hint(tr("A folder inside the emulator's folder can be given as a relative path. Several folders can be "
                              "given, separated by ; (semicolon). Pictures can also be in a zip file named after the folder "
                              "(snap.zip, flyers.zip...), in the folder or next to it.")))
        .row(nullptr, notAvailable_);

    Group back(tr("Background"));
    ui_->LabelImageCategoryBackgroundColor->setText(tr("Category Color"));
    ui_->ImageSingleBackgroundColorEnabled->setText(tr("Use Single Background Color"));
    back.row(ui_->LabelImageCategoryBackgroundColor, ui_->ImageCategoryBackgroundColor, {ui_->ButtonImageCategoryBackgroundColorReset})
        .row(nullptr, hint(tr("The color behind the selected category's pictures.")))
        .space(4)
        .check(ui_->ImageSingleBackgroundColorEnabled,
               hint(tr("Use the same background color behind every picture category, instead of a color for each one.")))
        .row(nullptr, ui_->ImageSingleBackgroundColor, {ui_->ImageSingleBackgroundColorButtonReset});

    settingsui::Page p("PicturesPage", tr("The picture categories: which ones have a button, where their pictures are, and the "
                                          "colors behind them."), 2);
    p.add(0, cats).add(1, sel).add(1, back).finish();
    settingsui::setPage(this, p.widget());
    showCategory();
    QTimer::singleShot(0, categories_, [this] { categories_->doItemsLayout(); categories_->viewport()->update(); });
}

} // namespace eld
