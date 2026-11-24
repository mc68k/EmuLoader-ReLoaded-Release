#include "NightModeDialog.h"
#include "FileProblem.h"

#include "ElWidgets.h"
#include "GameFonts.h"
#include "VclRuntime.h"
#include "SettingsLayout.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>

namespace eld {

NightModeDialog::NightModeDialog(const Input& in, QWidget* parent) : QDialog(parent), in_(in) {
    setObjectName("FormNightMode");
    setWindowTitle(tr("Customize Night Mode Colors"));
    buildPage();
    // the controls: by their names, NightMode<setting>
    for (ElColorBox* c : findChildren<ElColorBox*>()) {
        c->setProperty("elNoNight", true);
        connect(c, &ElColorBox::colorChanged, this, [this, c] {
            if (int* v = nm_.intValue(c->objectName())) *v = c->tcolor();
            changed();
        });
        // PopupNightModeCopyPasteColor: copy a color, paste it on another
        if (in_.colorMenu) {
            c->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(c, &QWidget::customContextMenuRequested, this, [this, c](const QPoint& at) {
                colorTarget_ = c->objectName();
                if (QAction* paste = in_.colorMenu->findChild<QAction*>("PopupNightModePasteColor"))
                    paste->setEnabled(QColor::fromString(QApplication::clipboard()->text().trimmed()).isValid());
                in_.colorMenu->exec(c->mapToGlobal(at));
            });
        }
    }
    if (in_.colorMenu) { // (the menu is the main window's: these go with this window)
        if (QAction* copy = in_.colorMenu->findChild<QAction*>("PopupNightModeCopyColor"))
            connect(copy, &QAction::triggered, this, [this] { copyColor(colorTarget_); });
        if (QAction* paste = in_.colorMenu->findChild<QAction*>("PopupNightModePasteColor"))
            connect(paste, &QAction::triggered, this, [this] { pasteColor(colorTarget_); });
    }
    for (QCheckBox* c : findChildren<QCheckBox*>())
        connect(c, &QCheckBox::toggled, this, [this, c](bool on) {
            if (bool* v = nm_.boolValue(c->objectName())) *v = on;
            changed();
        });
    for (QSlider* s : findChildren<QSlider*>())
        connect(s, &QSlider::valueChanged, this, [this, s](int v) {
            if (int* x = nm_.intValue(s->objectName())) *x = v;
            changed();
        });
    connect(listImage_, &QLineEdit::editingFinished, this, [this] {
        nm_.look.listImage = listImage_->text();
        changed();
    });
    connect(listImageSelect_, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Games List Background Image"), QString(),
                                                       tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (f.isEmpty()) return;
        listImage_->setText(QDir::toNativeSeparators(f));
        nm_.look.listImage = QDir::toNativeSeparators(f);
        changed();
    });
    // Choose Font: the header's and the game documents' (with their colors)
    auto fontButton = [this](const char* name, std::function<QFont()> get, std::function<void(const QFont&)> set, QString title) {
        auto* b = findChild<QPushButton*>(name);
        connect(b, &QPushButton::clicked, this, [this, get, set, title] {
            bool ok = false;
            const QFont f = QFontDialog::getFont(&ok, get(), this, title);
            if (!ok) return;
            set(f);
            changed();
        });
    };
    fontButton("NightModeGamesListHeaderFont", [this] { return nm_.look.headerFont(); },
               [this](const QFont& f) { nm_.look.setHeaderFont(f); }, tr("Columns Header Font"));
    fontButton("NightModeGameDocsFont_Setting", [this] { return nm_.look.docsFont(); },
               [this](const QFont& f) { nm_.look.setDocsFont(f); }, tr("Game Documents Font"));

    // each box's Default: its colors as they were at first
    for (QPushButton* b : findChildren<QPushButton*>())
        if (b->property("boxDefault").toBool())
            connect(b, &QPushButton::clicked, this, [this, b] { boxDefaults(b->property("box").value<QWidget*>()); });

    // the profiles
    connect(profiles_, &QComboBox::activated, this, [this] {
        const QString p = profiles_->currentText();
        if (modified_ && QMessageBox::question(this, windowTitle(), tr("Save the changes to \"%1\"?").arg(nm_profileShown_)) ==
                             QMessageBox::Yes)
            nm_.save(fileOf(nm_profileShown_));
        loadProfile(p);
        if (in_.apply) in_.apply(p);
    });
    findChild<QPushButton*>("NightModeProfiles_ButtonUpdate")->setText(QStringLiteral("\u21BB"));
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonUpdate"), &QPushButton::clicked, this, [this] { fillProfiles(profile()); });
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonReload"), &QPushButton::clicked, this, [this] {
        loadProfile(profile());
        if (in_.preview) in_.preview(nm_);
    });
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonDefault"), &QPushButton::clicked, this, [this] {
        nm_ = NightMode();
        show(nm_);
        changed();
    });
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonSave"), &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonDelete"), &QPushButton::clicked, this, [this] {
        if (profile().compare("Default", Qt::CaseInsensitive) == 0) return;
        if (QMessageBox::question(this, windowTitle(), tr("Delete the profile \"%1\"?").arg(profile())) != QMessageBox::Yes) return;
        deleteProfile();
    });
    connect(findChild<QPushButton*>("NightModeProfiles_ButtonNew"), &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Profile"), tr("Profile name:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (!newProfile(name, true))
            QMessageBox::warning(this, windowTitle(), tr("A profile can't have that name, or there's one with it already."));
    });

    fillProfiles(in.profile);
    loadProfile(in.profile);
}

NightModeDialog::~NightModeDialog() = default;

// The page: a framed group per part of the program, its colors by name (NightMode<setting>, as
// the original's controls), each group's Default; the profiles at the top.
void NightModeDialog::buildPage() {
    using namespace settingsui;
    auto label = [](const QString& t) { return new QLabel(t); };
    auto color = [](const char* name) {
        auto* c = new ElColorBox;
        c->setObjectName(name);
        c->setMinimumContentsLength(10); // (the columns fit the window)
        return c;
    };
    auto check = [](const char* name, const QString& t) {
        auto* c = new QCheckBox(t);
        c->setObjectName(name);
        return c;
    };
    auto rows = [&](Group& g, std::initializer_list<std::pair<QString, const char*>> list) {
        for (const auto& [t, n] : list) g.row(label(t), color(n));
    };
    auto subheading = [](const QString& t) {
        auto* l = new QLabel("<b>" + t.toHtmlEscaped() + "</b>");
        return l;
    };
    // a group's Default button (at the top right): its colors as at first
    auto withDefault = [](Group& g, std::initializer_list<QWidget*> first = {}) {
        auto* b = new QPushButton(tr("Default"));
        b->setObjectName(g.box()->objectName() + "Default");
        b->setToolTip(tr("These colors as they were at first"));
        b->setProperty("boxDefault", true);
        b->setProperty("box", QVariant::fromValue<QWidget*>(g.box()));
        b->setAutoDefault(false);
        g.line(first, b);
    };

    // the profiles
    profiles_ = new QComboBox;
    profiles_->setObjectName("NightModeProfiles");
    profiles_->setMinimumWidth(120);
    auto button = [](const char* name, const QString& t, const QString& tip = {}) {
        auto* b = new QPushButton(t);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setAutoDefault(false);
        return b;
    };
    Group profiles(tr("Profile"), "NightModeProfilesBox");
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(profiles_, 1);
        for (QWidget* b : {(QWidget*)button("NightModeProfiles_ButtonUpdate", QStringLiteral("\u21BB"), tr("Read the profiles folder again")),
                           (QWidget*)button("NightModeProfiles_ButtonReload", tr("Reload"), tr("The profile's colors as saved")),
                           (QWidget*)button("NightModeProfiles_ButtonDefault", tr("Default All"), tr("Every color as it was at first")),
                           (QWidget*)button("NightModeProfiles_ButtonNew", tr("New..."), tr("A new profile with these colors")),
                           (QWidget*)button("NightModeProfiles_ButtonSave", tr("Save")),
                           (QWidget*)button("NightModeProfiles_ButtonDelete", tr("Delete"))}) {
            b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            b->setMinimumWidth(0);
            b->setFixedWidth(std::max(28, b->fontMetrics().horizontalAdvance(static_cast<QPushButton*>(b)->text()) + 20));
            h->addWidget(b);
        }
        profiles.wide(row);
    }

    // column 1: the games list
    Group sel(tr("Games List Selection Bar"), "NightModeGamesSelectionBarColorsBox");
    withDefault(sel, {check("NightModeGameSelectionGradientBar", tr("Gradient"))});
    sel.line({check("NightModeGameSelectionAlphaBlend", tr("Alpha Blending")), check("NightModeGameSelectionRoundCorners", tr("Round Corners"))});
    rows(sel, {{tr("Top Background"), "NightModeGamesSelectionTopColor"}, {tr("Bottom Background"), "NightModeGamesSelectionBottomColor"},
               {tr("Frame"), "NightModeGamesSelectionFrameColor"}, {tr("Text"), "NightModeGamesSelectionFontColor"}});
    sel.text(subheading(tr("Games With Missing ROMs/CHDs")));
    rows(sel, {{tr("Top Background"), "NightModeGamesSelectionMissROMsTopColor"},
               {tr("Bottom Background"), "NightModeGamesSelectionMissROMsBottomColor"},
               {tr("Frame"), "NightModeGamesSelectionMissROMsFrameColor"}, {tr("Text"), "NightModeGamesSelectionMissROMsFontColor"}});
    sel.text(subheading(tr("When the List Isn't Focused")));
    rows(sel, {{tr("Top Background"), "NightModeGamesSelectionInactiveTopColor"},
               {tr("Bottom Background"), "NightModeGamesSelectionInactiveBottomColor"},
               {tr("Frame"), "NightModeGamesSelectionInactiveFrameColor"}, {tr("Text"), "NightModeGamesSelectionInactiveFontColor"}});

    Group header(tr("Games List Column Header"), "NightModeGamesListHeaderBox");
    withDefault(header, {button("NightModeGamesListHeaderFont", tr("Choose Font..."), tr("The column titles' font and its color"))});
    rows(header, {{tr("Background"), "NightModeGamesListHeaderBackgroundColor"},
                  {tr("Hover"), "NightModeGamesListHeaderBackgroundColorHover"},
                  {tr("Pressed"), "NightModeGamesListHeaderBackgroundColorDown"}});

    Group status(tr("Games List Status Bar"), "NightModeGamesListStatusBarColorsBox");
    withDefault(status, {check("NightModeGamesListStatusBarGradientBar", tr("Gradient"))});
    rows(status, {{tr("Top"), "NightModeGamesListStatusBarTopColor"}, {tr("Bottom"), "NightModeGamesListStatusBarBottomColor"},
                  {tr("Text"), "NightModeGamesListStatusBarFontColor"}});

    Group listSplit(tr("Games List / Machines List Splitter"), "NightModeGamesListSplittersBox");
    withDefault(listSplit, {check("NightModeGamesListSplitterShowGripIcon", tr("Show Grip Icon"))});
    rows(listSplit, {{tr("Color"), "NightModeGamesListSplitterSingleColor"}, {tr("Hover Color"), "NightModeGamesListSplitterSingleColorHot"}});

    // column 2: the tool bar and the panels
    Group toolBar(tr("Tool Bar Background"), "NightModeToolBarBackgroundColorsBox");
    withDefault(toolBar, {check("NightModeToolBarGradientBar", tr("Gradient"))});
    rows(toolBar, {{tr("Top"), "NightModeToolBarBkTopColor"}, {tr("Bottom"), "NightModeToolBarBkBottomColor"}});

    Group search(tr("Tool Bar Search Panel"), "NightModeSearchGamesPanelColorsBox");
    withDefault(search);
    rows(search, {{tr("Filter Text"), "NightModeSearchGamesPanelFilterFontColor"},
                  {tr("Field Text"), "NightModeSearchGamesPanelFieldFontColor"},
                  {tr("Search Box Text"), "NightModeSearchGamesPanelEditBoxFontColor"},
                  {tr("Search Box Background"), "NightModeSearchGamesPanelEditBoxBackgroundColor"},
                  {tr("Search Box Frame"), "NightModeSearchGamesPanelEditBoxCustomFrameColor"},
                  {tr("Frame While Typing"), "NightModeSearchGamesPanelEditBoxCustomFocusedFrameColor"}});

    Group panels(tr("Panels"), "NightModePanelColorsBox");
    withDefault(panels, {check("NightModeSearchGamesPanelGradientBar", tr("Gradient"))});
    rows(panels, {{tr("Top"), "NightModeSearchGamesPanelTopColor"}, {tr("Bottom"), "NightModeSearchGamesPanelBottomColor"},
                  {tr("Inner Frame"), "NightModeSearchGamesPanelInnerFrameColor"},
                  {tr("Outer Frame"), "NightModeSearchGamesPanelOuterFrameColor"},
                  {tr("Picture Category Text"), "NightModePanelColorsImageCategoryTextFontColor"},
                  {tr("Picture Zip File Text"), "NightModePanelColorsImageZipTextFontColor"},
                  {tr("Title Text"), "NightModePanelColorsTitleFontColor"}});
    panels.row(check("NightModePanelColorsTitleShadowEnabled", tr("Title Shadow")), color("NightModePanelColorsTitleShadowFontColor"));
    panels.row(label(tr("Second Title Text")), color("NightModePanelColorsTitle2FontColor"));
    panels.row(check("NightModePanelColorsTitle2ShadowEnabled", tr("Second Title Shadow")),
               color("NightModePanelColorsTitle2ShadowFontColor"));

    Group menus(tr("Menus"), "NightModeMenuPopupMenuBox");
    withDefault(menus);
    rows(menus, {{tr("Frame"), "NightModeMenuPopupMenuFrameColor"}});

    // column 3: the buttons and the pictures
    Group buttons(tr("Buttons"), "NightModeButtonColorsBox");
    withDefault(buttons);
    rows(buttons, {{tr("Text"), "NightModeButtonColorFontColor"}, {tr("Text (Grayed Out)"), "NightModeButtonColorFontColorDisabled"}});
    buttons.row(check("NightModeLabelButtonColorFontShadowColorDisabled", tr("Text Shadow (Grayed Out)")),
                color("NightModeButtonColorFontShadowColorDisabled"));
    rows(buttons, {{tr("Top"), "NightModeButtonColorGradientTop"}, {tr("Bottom"), "NightModeButtonColorGradientBottom"},
                   {tr("Top (Hover)"), "NightModeButtonColorGradientTopHover"},
                   {tr("Bottom (Hover)"), "NightModeButtonColorGradientBottomHover"},
                   {tr("Top (Selected)"), "NightModeButtonColorGradientTopSelected"},
                   {tr("Bottom (Selected)"), "NightModeButtonColorGradientBottomSelected"},
                   {tr("Top (Grayed Out)"), "NightModeButtonColorGradientTopDisabled"},
                   {tr("Bottom (Grayed Out)"), "NightModeButtonColorGradientBottomDisabled"},
                   {tr("Frame"), "NightModeButtonColorFrameColor"}, {tr("Frame (Hover)"), "NightModeButtonColorFrameColorHover"},
                   {tr("Frame (Selected)"), "NightModeButtonColorFrameColorSelected"},
                   {tr("Frame (Grayed Out)"), "NightModeButtonColorFrameColorDisabled"},
                   {tr("Frame (Focused)"), "NightModeButtonColorFrameColorFocused"}});

    Group hintBox(tr("Picture Hint Box"), "NightModeImagesHintBox");
    withDefault(hintBox);
    rows(hintBox, {{tr("Background"), "NightModeHintBox_Color"}});
    auto slider = [](const char* name, int min, int max, const QString& tip) {
        auto* s = new QSlider(Qt::Horizontal);
        s->setObjectName(name);
        s->setRange(min, max);
        s->setToolTip(tip);
        return s;
    };
    auto* opacity = slider("NightModeHintBox_Opacity", 0, 255, tr("How solid the hint box is: 0 is see-through, 255 solid"));
    auto* opacityValue = new QLabel;
    connect(opacity, &QSlider::valueChanged, opacityValue, [opacityValue](int v) { opacityValue->setText(QString::number(v)); });
    hintBox.row(label(tr("Opacity")), opacity, {opacityValue});
    hintBox.row(check("NightModeHintBox_FrameEnabled", tr("Frame")), color("NightModeHintBox_FrameColor"));
    rows(hintBox, {{tr("Text"), "NightModeHintBox_TextColor"}});
    hintBox.row(check("NightModeHintBox_TextShadowEnabled", tr("Text Shadow")), color("NightModeHintBox_TextShadowColor"));
    hintBox.check(check("NightModeHintBox_FontItalicStyle", tr("Italic")));
    hintBox.row(label(tr("Position")), slider("NightModeHintBox_Position", 1, 8, tr("Where the hint box is in the picture")));

    Group picSplit(tr("Picture Splitters"), "NightModeImageSplittersBox");
    withDefault(picSplit, {check("NightModeImageSplitterShowGripIcon", tr("Show Grip Icon"))});
    rows(picSplit, {{tr("Color"), "NightModeImageSplitterSingleColor"}, {tr("Hover Color"), "NightModeImageSplitterSingleColorHot"}});

    Group border(tr("Picture Border"), "NightModeImageBorderColorBox");
    withDefault(border);
    rows(border, {{tr("Border"), "NightModeImageBorderColor"}});

    // across: the games list's background, the game documents
    Group back(tr("Games List Background"), "NightModeGamesListBackgroundBox");
    withDefault(back);
    rows(back, {{tr("Background Color"), "NightModeGamesBackgroundColor"}});
    listImage_ = new QLineEdit;
    listImage_->setObjectName("NightModeGamesBackgroundImage");
    listImageSelect_ = button("NightModeGamesBackgroundImageButtonSelect", tr("Select..."));
    back.check(check("NightModeGamesBackgroundImageEnable", tr("Use a Picture as the Background (.png)")));
    back.row(label(tr("Picture")), listImage_, {listImageSelect_});
    back.check(check("NightModeGamesTileBackground", tr("Tile the Picture")));

    Group docs(tr("MAME Game Documents"), "NightModeMAMEGameDocsBox");
    withDefault(docs, {button("NightModeGameDocsFont_Setting", tr("Choose Font..."), tr("The documents' font and its color"))});
    rows(docs, {{tr("Background"), "NightModeGameDocumentsBackgroundColor"}, {tr("Border"), "NightModeGameDocsBorderColor"}});
    docs.check(check("NightModeGameDocsShowStatusBar", tr("Show the Status Bar")));

    Page page("NightModePage", tr("The colors of Night Mode, saved in a profile: each change shows at once while Night Mode is on."), 2);
    page.top(profiles)
        .add(0, sel).add(0, header).add(0, status).add(0, back).add(0, listSplit).add(0, hintBox).add(0, picSplit).add(0, border)
        .add(1, toolBar).add(1, search).add(1, panels).add(1, menus).add(1, buttons).add(1, docs)
        .finish();
    setPage(this, page.widget());
}

QString NightModeDialog::fileOf(const QString& p) const { return QDir(in_.folder).filePath(p + ".ini"); }

QString NightModeDialog::profile() const { return nm_profileShown_; }

void NightModeDialog::fillProfiles(const QString& select) {
    const QSignalBlocker b(profiles_);
    profiles_->clear();
    profiles_->addItems(NightMode::profiles(in_.folder));
    const int i = profiles_->findText(select, Qt::MatchFixedString);
    profiles_->setCurrentIndex(std::max(0, i));
}

void NightModeDialog::loadProfile(const QString& name) {
    nm_ = NightMode();
    nm_.load(fileOf(name));
    nm_profileShown_ = name;
    fillProfiles(name);
    show(nm_);
    modified_ = false;
}

void NightModeDialog::show(const NightMode& nmIn) {
    NightMode nm = nmIn; // the controls' signals write into nm_; keep the values to show
    loading_ = true;
    for (ElColorBox* c : findChildren<ElColorBox*>())
        if (int* v = nm.intValue(c->objectName())) c->setTColor(*v);
    for (QCheckBox* c : findChildren<QCheckBox*>())
        if (bool* v = nm.boolValue(c->objectName())) c->setChecked(*v);
    for (QSlider* s : findChildren<QSlider*>())
        if (int* v = nm.intValue(s->objectName())) s->setValue(*v);
    listImage_->setText(nm.look.listImage);
    nm_ = nm;
    loading_ = false;
}

void NightModeDialog::changed() {
    if (loading_) return;
    modified_ = true;
    if (in_.preview) in_.preview(nm_);
}

// a box's Default: its controls as the original's night defaults
void NightModeDialog::boxDefaults(QWidget* box) {
    NightMode d;
    loading_ = true;
    for (ElColorBox* c : box->findChildren<ElColorBox*>())
        if (int* v = d.intValue(c->objectName())) { c->setTColor(*v); *nm_.intValue(c->objectName()) = *v; }
    for (QCheckBox* c : box->findChildren<QCheckBox*>())
        if (bool* v = d.boolValue(c->objectName())) { c->setChecked(*v); *nm_.boolValue(c->objectName()) = *v; }
    for (QSlider* s : box->findChildren<QSlider*>())
        if (int* v = d.intValue(s->objectName())) { s->setValue(*v); *nm_.intValue(s->objectName()) = *v; }
    if (box == findChild<QWidget*>("NightModeGamesListHeaderBox")) {
        nm_.look.headerFontName = d.look.headerFontName;
        nm_.look.headerFontSize = d.look.headerFontSize;
        nm_.look.headerFontStyle = d.look.headerFontStyle;
        nm_.look.headerFontColor = d.look.headerFontColor;
    }
    if (box == findChild<QWidget*>("NightModeMAMEGameDocsBox")) {
        nm_.look.docsFontName = d.look.docsFontName;
        nm_.look.docsFontSize = d.look.docsFontSize;
        nm_.look.docsFontStyle = d.look.docsFontStyle;
        nm_.look.docsFontColor = d.look.docsFontColor;
    }
    if (box == findChild<QWidget*>("NightModeGamesListBackgroundBox")) {
        nm_.look.listImage = d.look.listImage;
        listImage_->setText(d.look.listImage);
    }
    loading_ = false;
    changed();
}

void NightModeDialog::setColor(const QString& control, int tcolor) {
    if (auto* c = findChild<ElColorBox*>(control)) c->setTColor(tcolor);
    if (int* v = nm_.intValue(control)) *v = tcolor;
    changed();
}

// The copied color, so a system color (the Windows button face, ...) is pasted as itself, not
// as the RGB it has now.
static QString copiedName;
static int copiedTColor = 0;

void NightModeDialog::copyColor(const QString& control) {
    auto* c = findChild<ElColorBox*>(control);
    if (!c) return;
    copiedTColor = c->tcolor();
    copiedName = c->color().name();
    QApplication::clipboard()->setText(copiedName);
}

bool NightModeDialog::pasteColor(const QString& control) {
    const QString text = QApplication::clipboard()->text().trimmed();
    const QColor col = QColor::fromString(text);
    if (!col.isValid() || !findChild<ElColorBox*>(control)) return false;
    setColor(control, text.compare(copiedName, Qt::CaseInsensitive) == 0 && !copiedName.isEmpty()
                          ? copiedTColor : ElColorBox::fromQColor(col));
    return true;
}

void NightModeDialog::selectProfile(const QString& name) {
    loadProfile(name);
    if (in_.apply) in_.apply(name);
}

bool NightModeDialog::saveProfile() {
    if (!nm_.save(fileOf(profile()))) {
        FileProblem::cantSave(this, windowTitle(), fileOf(profile()));
        return false;
    }
    modified_ = false;
    if (in_.apply) in_.apply(profile());
    return true;
}

bool NightModeDialog::newProfile(const QString& name, bool copyColors) {
    static const QRegularExpression bad(R"([\\/:*?"<>|])");
    if (name.contains(bad) || QFile::exists(fileOf(name))) return false;
    NightMode n = copyColors ? nm_ : NightMode();
    if (!n.save(fileOf(name))) return false;
    nm_ = n;
    nm_profileShown_ = name;
    fillProfiles(name);
    modified_ = false;
    if (in_.apply) in_.apply(name);
    return true;
}

bool NightModeDialog::deleteProfile() {
    if (profile().compare("Default", Qt::CaseInsensitive) == 0) return false;
    if (!QFile::remove(fileOf(profile()))) return false;
    loadProfile("Default");
    if (in_.apply) in_.apply("Default");
    return true;
}

// closing, as the other Settings tabs: OK saves the changes to the profile, Cancel puts its saved
// colors back (no question)
void NightModeDialog::done(int r) {
    if (modified_) {
        if (r == QDialog::Accepted) {
            if (!saveProfile()) return; // (it said why: the window stays)
        } else if (in_.apply) {
            in_.apply(profile()); // the saved colors again
        }
    }
    QDialog::done(r);
}

} // namespace eld
