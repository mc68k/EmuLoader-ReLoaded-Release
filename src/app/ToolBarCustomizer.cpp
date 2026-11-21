#include "ToolBarCustomizer.h"

#include "SettingsLayout.h"
#include "WinIni.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QApplication>
#include <QFrame>
#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

namespace eld {

namespace {
constexpr int kTokenRole = Qt::UserRole + 1;
const QString kSeparator = QStringLiteral("|");
const QString kSpace = QStringLiteral("space");

QIcon drawn(bool separator) {
    QPixmap p(48, 48);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(110, 110, 110), 2);
    g.setPen(pen);
    if (separator) {
        g.drawLine(24, 6, 24, 42);
    } else { // a space: an arrow both ways
        g.drawLine(8, 24, 40, 24);
        g.drawLine(8, 24, 15, 17);
        g.drawLine(8, 24, 15, 31);
        g.drawLine(40, 24, 33, 17);
        g.drawLine(40, 24, 33, 31);
    }
    return QIcon(p);
}

void setupList(QListWidget* l, bool wrap) {
    l->setViewMode(QListView::IconMode);
    l->setMovement(QListView::Static); // (rows move on a drop, not free positions)
    l->setFlow(QListView::LeftToRight);
    l->setWrapping(wrap);
    l->setResizeMode(QListView::Adjust);
    l->setIconSize(QSize(40, 40));
    l->setGridSize(QSize(84, 84));
    l->setWordWrap(true);
    l->setSelectionMode(QAbstractItemView::SingleSelection);
    l->setDragDropMode(QAbstractItemView::NoDragDrop); // (dragged with the mouse here: ToolBarCustomizer::eventFilter)
}
} // namespace

ToolBarCustomizer::ToolBarCustomizer(const Input& in, QWidget* parent) : QWidget(parent), in_(in) {
    setObjectName("ToolBarCustomizer");
    using settingsui::Group;
    QLabel* like = new QLabel(this);
    like->setStyleSheet("color: #6E6E6E;");
    like->hide();
    auto hint = [like](const QString& t) { return settingsui::hint(t, like); };
    auto button = [](const QString& text, const char* name) {
        auto* b = new QPushButton(text);
        b->setObjectName(name);
        b->setAutoDefault(false);
        return b;
    };

    // The tool bar
    bar_ = new QListWidget;
    bar_->setObjectName("ToolBarStrip");
    setupList(bar_, false);
    bar_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    bar_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bar_->setFixedHeight(84 + 24);
    bar_->setMinimumWidth(5 * 84); // (it scrolls sideways when the buttons don't fit)
    QPushButton* left = button(tr("Move Left"), "ToolBarMoveLeft");
    QPushButton* right = button(tr("Move Right"), "ToolBarMoveRight");
    QPushButton* take = button(tr("Remove"), "ToolBarRemove");
    connect(left, &QPushButton::clicked, this, [this] { move(bar_->currentRow(), -1); });
    connect(right, &QPushButton::clicked, this, [this] { move(bar_->currentRow(), 1); });
    connect(take, &QPushButton::clicked, this, [this] { remove(bar_->currentRow()); });
    Group strip(tr("Your Tool Bar"));
    strip.wide(bar_)
        .line({left, right, take})
        .text(hint(tr("Drag a button to move it, drag it down to Available Buttons to take it off the tool bar, or drag "
                      "one up from there to add it. Or select one and use the buttons. A space pushes the buttons after "
                      "it to the right end; a separator draws a line between buttons.")));

    // The buttons that aren't on it, a separator and a space
    palette_ = new QListWidget;
    palette_->setObjectName("ToolBarPalette");
    setupList(palette_, true);
    palette_->setMinimumHeight(2 * 84 + 12);
    QPushButton* addButton = button(tr("Add to Tool Bar"), "ToolBarAdd");
    auto addCurrent = [this] {
        if (QListWidgetItem* it = palette_->currentItem()) {
            const int at = bar_->currentRow() >= 0 ? bar_->currentRow() + 1 : -1;
            add(it->data(kTokenRole).toString(), at);
        }
    };
    connect(addButton, &QPushButton::clicked, this, addCurrent);
    connect(palette_, &QListWidget::itemDoubleClicked, this, addCurrent);
    Group available(tr("Available Buttons"));
    available.wide(palette_).line({addButton});

    // Presets
    presets_ = new QComboBox;
    presets_->setObjectName("ToolBarPresets");
    presets_->setMinimumWidth(220);
    QPushButton* save = button(tr("Save As Preset..."), "ToolBarSavePreset");
    QPushButton* del = button(tr("Delete Preset"), "ToolBarDeletePreset");
    connect(presets_, &QComboBox::activated, this, [this] { usePreset(presets_->currentData().toString()); });
    connect(save, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save As Preset"), tr("A name for this tool bar:"), QLineEdit::Normal,
                                                   QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        savePreset(name);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const QString name = presets_->currentData().toString();
        if (!savedPresets().contains(name)) {
            QMessageBox::information(this, tr("Delete Preset"), tr("Default and Original can't be deleted."));
            return;
        }
        deletePreset(name);
    });
    auto syncDelete = [this, del] { del->setEnabled(savedPresets().contains(presets_->currentData().toString())); };
    connect(presets_, &QComboBox::currentIndexChanged, this, syncDelete);
    Group presets(tr("Presets"));
    presets.line({presets_, save, del})
        .text(hint(tr("Default is the original's buttons with MAME Problems and the Guide at the right end. Original is the order of "
                      "EmuLoader 8.9.4. Save your own arrangements to switch between them.")));

    // Options
    auto* showBar = new QCheckBox(tr("Show Tool Bar"));
    showBar->setObjectName("ShowToolBar");
    showBar->setChecked(in_.showBar);
    connect(showBar, &QCheckBox::toggled, this, [this](bool on) { if (in_.setShowBar) in_.setShowBar(on); });
    auto* bound = new QCheckBox(tr("Bound to Games Panel"));
    bound->setObjectName("BoundToGamesPanel");
    bound->setChecked(in_.bound);
    connect(bound, &QCheckBox::toggled, this, [this](bool on) { if (in_.setBound) in_.setBound(on); });
    auto* search = new QCheckBox(tr("Show the Search Games Box"));
    search->setObjectName("ShowSearchGames");
    search->setChecked(in_.showSearch);
    connect(search, &QCheckBox::toggled, this, [this](bool on) { if (in_.setShowSearch) in_.setShowSearch(on); });
    auto* sizes = new QButtonGroup(this);
    QList<QRadioButton*> sizeButtons;
    const std::pair<const char*, const char*> sizeNames[] = {{QT_TR_NOOP("Extra Large (68×68)"), "IconSizeExtraLarge"},
                                                             {QT_TR_NOOP("Large (48×48)"), "IconSizeLarge"},
                                                             {QT_TR_NOOP("Small (30×24)"), "IconSizeSmall"}};
    for (int i = 0; i < 3; ++i) {
        auto* r = new QRadioButton(tr(sizeNames[i].first));
        r->setObjectName(sizeNames[i].second);
        r->setChecked(i == in_.iconSize);
        sizes->addButton(r, i);
        sizeButtons << r;
    }
    connect(sizes, &QButtonGroup::idClicked, this, [this](int i) { if (in_.setIconSize) in_.setIconSize(i); });
    Group options(tr("Options"));
    options.check(showBar)
        .check(bound, hint(tr("The tool bar sits above the games list instead of across the whole window.")))
        .check(search, hint(tr("The title search box at the right of the tool bar.")))
        .space(4)
        .row(new QLabel(tr("Icon Size")), sizeButtons[0])
        .row(nullptr, sizeButtons[1])
        .row(nullptr, sizeButtons[2]);

    settingsui::Page p("ToolBarPage", tr("The tool bar's buttons and their order. Changes show at once; Cancel puts the tool bar "
                                         "back as it was."), 2);
    p.top(strip).add(0, available).add(1, presets).add(1, options).finish();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(p.widget());

    connect(bar_->model(), &QAbstractItemModel::rowsInserted, this, &ToolBarCustomizer::barChanged);
    connect(bar_->model(), &QAbstractItemModel::rowsRemoved, this, &ToolBarCustomizer::barChanged);
    connect(bar_->model(), &QAbstractItemModel::rowsMoved, this, &ToolBarCustomizer::barChanged);
    connect(palette_->model(), &QAbstractItemModel::rowsInserted, this, [this] {
        if (!filling_) QTimer::singleShot(0, this, [this] { fillPalette(); }); // a button dropped there: taken off the bar
    });
    // Dragging a button (eventFilter)
    bar_->viewport()->installEventFilter(this);
    palette_->viewport()->installEventFilter(this);
    fillBar(in_.order);
    fillPalette();
    fillPresets();
    syncDelete();
    adjustSize();
}

QListWidgetItem* ToolBarCustomizer::makeItem(const QString& token) const {
    auto* it = new QListWidgetItem;
    it->setData(kTokenRole, token);
    it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    if (token == kSeparator) {
        it->setText(tr("Separator"));
        it->setIcon(drawn(true));
        it->setToolTip(tr("A line between buttons"));
    } else if (token == kSpace) {
        it->setText(tr("Space"));
        it->setIcon(drawn(false));
        it->setToolTip(tr("Pushes the buttons after it to the right end"));
    } else {
        for (const Tool& t : in_.tools)
            if (t.id == token) {
                it->setText(t.caption.isEmpty() ? t.title : t.caption);
                it->setIcon(t.icon);
                it->setToolTip(t.detail.isEmpty() ? t.title : t.title + "\n" + t.detail);
            }
    }
    return it;
}

QStringList ToolBarCustomizer::order() const {
    QStringList out;
    for (int i = 0; i < bar_->count(); ++i) out << bar_->item(i)->data(kTokenRole).toString();
    return out;
}

void ToolBarCustomizer::fillBar(const QStringList& order) {
    filling_ = true;
    const int row = bar_->currentRow();
    bar_->clear();
    for (const QString& t : order) bar_->addItem(makeItem(t));
    if (row >= 0 && bar_->count()) bar_->setCurrentRow(std::min(row, bar_->count() - 1));
    filling_ = false;
}

void ToolBarCustomizer::fillPalette() {
    filling_ = true;
    const QString cur = palette_->currentItem() ? palette_->currentItem()->data(kTokenRole).toString() : QString();
    palette_->clear();
    const QStringList on = order();
    for (const Tool& t : in_.tools)
        if (!on.contains(t.id)) palette_->addItem(makeItem(t.id));
    palette_->addItem(makeItem(kSeparator));
    palette_->addItem(makeItem(kSpace));
    for (int i = 0; i < palette_->count(); ++i)
        if (palette_->item(i)->data(kTokenRole).toString() == cur) palette_->setCurrentRow(i);
    filling_ = false;
}

// After a drop, a move or a removal: a button once only, then the tool bar, the palette
void ToolBarCustomizer::barChanged() {
    if (filling_ || pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] {
        pending_ = false;
        QStringList clean;
        QSet<QString> seen;
        bool dropped = false;
        for (const QString& t : order()) {
            if (t.isEmpty()) continue;
            if (t != kSeparator && t != kSpace) {
                if (seen.contains(t)) {
                    dropped = true;
                    continue;
                }
                seen.insert(t);
            }
            clean << t;
        }
        if (dropped) fillBar(clean);
        if (in_.setOrder) in_.setOrder(clean);
        fillPalette();
    });
}

void ToolBarCustomizer::setOrder(const QStringList& order) {
    fillBar(order);
    if (in_.setOrder) in_.setOrder(this->order());
    fillPalette();
}

int ToolBarCustomizer::gapAt(const QPoint& pos) const {
    for (int i = 0; i < bar_->count(); ++i) {
        const QRect r = bar_->visualItemRect(bar_->item(i));
        if (pos.x() < r.center().x()) return i;
    }
    return bar_->count();
}

bool ToolBarCustomizer::eventFilter(QObject* o, QEvent* e) {
    QListWidget* list = o == bar_->viewport() ? bar_ : o == palette_->viewport() ? palette_ : nullptr;
    if (!list) return QWidget::eventFilter(o, e);
    switch (e->type()) {
    case QEvent::MouseButtonPress: {
        auto* m = static_cast<QMouseEvent*>(e);
        if (m->button() != Qt::LeftButton) break;
        pressedIn_ = list;
        pressedItem_ = list->itemAt(m->position().toPoint());
        pressedAt_ = m->globalPosition().toPoint();
        break; // (and the list selects it)
    }
    case QEvent::MouseMove: {
        auto* m = static_cast<QMouseEvent*>(e);
        if (!pressedItem_ || !(m->buttons() & Qt::LeftButton)) break;
        const QPoint g = m->globalPosition().toPoint();
        if (!dragging_ && (g - pressedAt_).manhattanLength() < QApplication::startDragDistance()) break;
        dragTo(g);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto* m = static_cast<QMouseEvent*>(e);
        if (m->button() != Qt::LeftButton) break;
        if (dragging_) {
            dropAt(m->globalPosition().toPoint());
            endDrag();
            return true;
        }
        endDrag();
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(o, e);
}

void ToolBarCustomizer::dragTo(const QPoint& global) {
    if (!dragging_) {
        dragging_ = true;
        ghost_ = new QLabel(this);
        ghost_->setObjectName("ToolBarDragPicture");
        ghost_->setPixmap(pressedItem_->icon().pixmap(40, 40));
        ghost_->setAttribute(Qt::WA_TransparentForMouseEvents);
        ghost_->adjustSize();
        marker_ = new QFrame(bar_->viewport());
        marker_->setObjectName("ToolBarDropMarker");
        marker_->setStyleSheet("background: palette(highlight);");
        marker_->setAttribute(Qt::WA_TransparentForMouseEvents);
        pressedIn_->viewport()->setCursor(Qt::ClosedHandCursor);
    }
    ghost_->move(mapFromGlobal(global) - QPoint(ghost_->width() / 2, ghost_->height() / 2));
    ghost_->show();
    ghost_->raise();
    // the line where it goes, when over the tool bar
    const QPoint inBar = bar_->viewport()->mapFromGlobal(global);
    if (bar_->viewport()->rect().contains(inBar)) {
        const int gap = gapAt(inBar);
        int x = 2;
        if (bar_->count()) {
            const QRect r = bar_->visualItemRect(bar_->item(std::min(gap, bar_->count() - 1)));
            x = gap < bar_->count() ? r.left() : r.right();
        }
        marker_->setGeometry(std::max(0, x - 1), 4, 3, bar_->viewport()->height() - 8);
        marker_->show();
    } else {
        marker_->hide();
    }
}

// Dropped on the tool bar: moved along it, or added from Available Buttons; dropped on Available
// Buttons from the tool bar: taken off it
void ToolBarCustomizer::dropAt(const QPoint& global) {
    if (!pressedItem_ || !pressedIn_) return;
    const QPoint inBar = bar_->viewport()->mapFromGlobal(global);
    const QPoint inPalette = palette_->viewport()->mapFromGlobal(global);
    if (bar_->viewport()->rect().contains(inBar)) {
        const int gap = gapAt(inBar);
        if (pressedIn_ == bar_) {
            const int from = bar_->row(pressedItem_);
            if (from < 0) return;
            const int to = gap > from ? gap - 1 : gap;
            if (to != from) {
                QStringList o = order();
                o.move(from, to);
                setOrder(o);
            }
            bar_->setCurrentRow(to);
        } else {
            add(pressedItem_->data(kTokenRole).toString(), gap);
        }
    } else if (palette_->viewport()->rect().contains(inPalette) && pressedIn_ == bar_) {
        remove(bar_->row(pressedItem_));
    }
}

void ToolBarCustomizer::endDrag() {
    if (pressedIn_) pressedIn_->viewport()->unsetCursor();
    delete ghost_;
    delete marker_;
    ghost_ = nullptr;
    marker_ = nullptr;
    dragging_ = false;
    pressedIn_ = nullptr;
    pressedItem_ = nullptr;
}

void ToolBarCustomizer::add(const QString& token, int at) {
    QStringList o = order();
    if (token != kSeparator && token != kSpace && o.contains(token)) return;
    if (at < 0 || at > o.size()) at = o.size();
    o.insert(at, token);
    setOrder(o);
    bar_->setCurrentRow(at);
}

void ToolBarCustomizer::remove(int index) {
    QStringList o = order();
    if (index < 0 || index >= o.size()) return;
    o.removeAt(index);
    setOrder(o);
    if (bar_->count()) bar_->setCurrentRow(std::min(index, bar_->count() - 1));
}

void ToolBarCustomizer::move(int index, int delta) {
    QStringList o = order();
    const int to = index + delta;
    if (index < 0 || index >= o.size() || to < 0 || to >= o.size()) return;
    o.move(index, to);
    setOrder(o);
    bar_->setCurrentRow(to);
}

QStringList ToolBarCustomizer::savedPresets() const {
    WinIni ini;
    ini.load(in_.presetsFile);
    return ini.keys("Presets");
}

void ToolBarCustomizer::fillPresets(const QString& current) {
    presets_->clear();
    presets_->addItem(tr("Default"), "Default");
    presets_->addItem(tr("Original (EmuLoader 8.9.4)"), "Original");
    for (const QString& n : savedPresets()) presets_->addItem(n, n);
    const int i = presets_->findData(current.isEmpty() ? QStringLiteral("Default") : current);
    presets_->setCurrentIndex(std::max(0, i));
}

void ToolBarCustomizer::usePreset(const QString& name) {
    if (name == "Default") setOrder(in_.defaults);
    else if (name == "Original") setOrder(in_.original);
    else {
        WinIni ini;
        ini.load(in_.presetsFile);
        const QString o = ini.readString("Presets", name);
        if (!o.isEmpty()) setOrder(o.split(',', Qt::SkipEmptyParts));
    }
    fillPresets(name);
}

void ToolBarCustomizer::savePreset(const QString& name) {
    WinIni ini;
    ini.load(in_.presetsFile);
    ini.writeString("Presets", name, order().join(','));
    QDir().mkpath(QFileInfo(in_.presetsFile).absolutePath());
    ini.save(in_.presetsFile);
    fillPresets(name);
}

void ToolBarCustomizer::deletePreset(const QString& name) {
    WinIni ini;
    ini.load(in_.presetsFile);
    ini.deleteKey("Presets", name);
    ini.save(in_.presetsFile);
    fillPresets();
}

} // namespace eld
