#include "ColumnsEditorDialog.h"

#include "GamesModel.h"
#include "VclRuntime.h"
#include "ui_FormColumnsEditor.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eld {

namespace {
enum { ColName, ColWidth };
constexpr int ColumnRole = Qt::UserRole;

QString caption(int c) {
    if (c == GamesModel::Played) return GamesModel::columnTitle(c) + " " + QCoreApplication::translate("eld::ColumnsEditorDialog", "(count)");
    if (c == GamesModel::LastPlayed) return GamesModel::columnTitle(c) + " " + QCoreApplication::translate("eld::ColumnsEditorDialog", "(date)");
    return GamesModel::columnTitle(c);
}

// the width column: numbers only, at least 10 (ColumnsListViewItemEdited)
class WidthDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        if (i.column() != ColWidth) return nullptr;
        auto* e = new QLineEdit(parent);
        e->setValidator(new QIntValidator(1, 9999, e));
        (void)o;
        return e;
    }
    void setModelData(QWidget* editor, QAbstractItemModel* m, const QModelIndex& i) const override {
        const QString t = static_cast<QLineEdit*>(editor)->text();
        if (t.isEmpty()) return;
        m->setData(i, QString::number(std::max(10, t.toInt())));
    }
};
} // namespace

ColumnsEditorDialog::ColumnsEditorDialog(const Input& in, QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormColumnsEditor), in_(in) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Customize Columns"));

    list_ = new QTreeWidget(this);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setHeaderLabels({tr("Column"), tr("Width")});
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    list_->header()->resizeSection(ColWidth, 52);
    list_->setItemDelegate(new WidthDelegate(list_));
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->installEventFilter(this);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { editWidth(); });
    connect(list_, &QTreeWidget::itemChanged, this, [](QTreeWidgetItem* it, int col) {
        if (col != ColName) return;
        // Title always shows (ColumnsListViewItemCheckChange); unchecked columns are gray
        if (it->data(ColName, ColumnRole).toInt() == GamesModel::Title && it->checkState(ColName) != Qt::Checked)
            it->setCheckState(ColName, Qt::Checked);
        const QBrush b = it->checkState(ColName) == Qt::Checked ? QBrush() : QBrush(Qt::gray);
        it->setForeground(ColName, b);
        it->setForeground(ColWidth, b);
    });

    ui_->LabelTips->setText(tr("You can also open this window from the columns header's menu (right-click on the "
                               "column titles)."));
    connect(ui_->ButtonUp, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(ui_->ButtonDown, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(ui_->ButtonEditWidth, &QPushButton::clicked, this, &ColumnsEditorDialog::editWidth);
    connect(ui_->ButtonSizeDecrease, &QPushButton::clicked, this, [this] { addWidth(-1); });
    connect(ui_->ButtonSizeIncrease, &QPushButton::clicked, this, [this] { addWidth(1); });
    connect(ui_->ButtonDefaultSize, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* it = selected())
            it->setText(ColWidth, QString::number(GamesModel::defaultWidth(it->data(ColName, ColumnRole).toInt())));
    });
    connect(ui_->ButtonResetSize, &QPushButton::clicked, this, [this] {
        if (QTreeWidgetItem* it = selected()) {
            const int c = it->data(ColName, ColumnRole).toInt();
            const int w = in_.header->isSectionHidden(c) ? GamesModel::defaultWidth(c) : in_.header->sectionSize(c);
            it->setText(ColWidth, QString::number(w));
        }
    });
    connect(ui_->ButtonReloadProfile, &QPushButton::clicked, this, [this] { load(false); });
    connect(ui_->ButtonDefault, &QPushButton::clicked, this, &ColumnsEditorDialog::defaultAll);
    connect(ui_->ButtonCustomizeColumnHeaderFont, &QPushButton::clicked, this, [this] { if (in_.customizeFont) in_.customizeFont(); });
    connect(ui_->ButtonCustomizeColumnHeaderFontSetDefault, &QPushButton::clicked, this, [this] { if (in_.defaultFont) in_.defaultFont(); });
    connect(ui_->ButtonOk, &QPushButton::clicked, this, [this] {
        if (editing()) return;
        apply();
        accept();
    });
    connect(ui_->ButtonCancel, &QPushButton::clicked, this, &QDialog::reject);
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);
    layoutWindow();
    load(false);
    list_->setFocus();
}

ColumnsEditorDialog::~ColumnsEditorDialog() = default;

// In layouts: the columns list on the left (it takes the window's extra height); on the right the
// tip, the buttons (as wide as the widest, the pairs side by side) with their keys after them, the
// header font buttons, the note, and OK / Cancel. Every text whole in any language and size.
void ColumnsEditorDialog::layoutWindow() {
    using vcl::boxed, vcl::loose;
    auto pair = [](QPushButton* a, QPushButton* b) {
        auto* w = new QWidget;
        auto* h = boxed(new QHBoxLayout(w), 0, 0, 0, 0, 1);
        for (QPushButton* x : {a, b}) h->addWidget(vcl::textButton(x), 1);
        return w;
    };
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(3);
    int row = 0;
    auto line = [&](QWidget* button, QLabel* keys) {
        if (button) grid->addWidget(button, row, 0);
        if (keys) {
            loose(keys);
            keys->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(keys, row, 1, Qt::AlignVCenter);
        }
        ++row;
    };
    line(pair(ui_->ButtonUp, ui_->ButtonDown), ui_->LabelButtonUpDown);
    line(vcl::textButton(ui_->ButtonEditWidth), ui_->LabelButtonEditWidth);
    line(pair(ui_->ButtonSizeDecrease, ui_->ButtonSizeIncrease), ui_->LabelButtonSize);
    line(vcl::textButton(ui_->ButtonResetSize), ui_->LabelButtonResetSize);
    line(vcl::textButton(ui_->ButtonDefaultSize), ui_->LabelButtonDefaultSize);
    line(nullptr, ui_->LabelToggleVisibility);
    grid->setRowMinimumHeight(row++, 12);
    line(vcl::textButton(ui_->ButtonReloadProfile), ui_->LabelButtonReloadProfileDefaultSettings);
    line(vcl::textButton(ui_->ButtonDefault), ui_->LabelButtonSetDefaultAll);
    grid->setColumnMinimumWidth(0, 80);
    grid->setColumnStretch(2, 1);

    auto* fonts = boxed(new QHBoxLayout, 0, 0, 0, 0, 2);
    fonts->addWidget(vcl::textButton(ui_->ButtonCustomizeColumnHeaderFont));
    fonts->addWidget(vcl::textButton(ui_->ButtonCustomizeColumnHeaderFontSetDefault));
    fonts->addStretch();

    QWidget* tip = ui_->PanelTitleTip;
    loose(tip);
    loose(ui_->LabelTitleTip);
    ui_->LabelTitleTip->setWordWrap(true);
    boxed(new QVBoxLayout(tip), 9, 2, 4, 4, 0)->addWidget(ui_->LabelTitleTip);
    loose(ui_->LabelTips);
    ui_->LabelTips->setWordWrap(true);

    QWidget* bottom = ui_->PanelBottom;
    loose(bottom);
    auto* b = boxed(new QHBoxLayout(bottom), 8, 8, 8, 8, 11);
    b->addStretch();
    b->addWidget(vcl::textButton(ui_->ButtonOk));
    b->addWidget(vcl::textButton(ui_->ButtonCancel));
    b->addStretch();

    auto* right = boxed(new QVBoxLayout, 0, 0, 0, 0, 0);
    right->addWidget(tip);
    auto* inner = boxed(new QVBoxLayout, 11, 6, 12, 0, 0);
    inner->addLayout(grid);
    inner->addSpacing(16);
    inner->addLayout(fonts);
    inner->addSpacing(16);
    inner->addWidget(ui_->LabelTips);
    inner->addStretch(1);
    right->addLayout(inner, 1);
    right->addWidget(bottom);
    right->addStrut(248); // (the design's width at least)

    loose(list_);
    list_->setMinimumSize(218, 200);
    auto* root = boxed(new QHBoxLayout(this), 0, 0, 0, 0, 0);
    root->addWidget(list_, 1);
    root->addLayout(right);
    for (QWidget* x : {(QWidget*)this, tip, bottom}) x->setProperty("vclLaidOut", true);
    layout()->activate();
    resize(QSize(466, 464).expandedTo(minimumSizeHint())); // (the window as designed, or as the texts need)
}

// ResetColumns: as the list shows them now, or the defaults
void ColumnsEditorDialog::load(bool defaults) {
    const QSignalBlocker block(list_);
    list_->clear();
    QHeaderView* h = in_.header;
    for (int v = 0; v < GamesModel::ColumnCount; ++v) {
        const int c = defaults ? v : h->logicalIndex(v);
        auto* it = new QTreeWidgetItem(list_);
        it->setText(ColName, caption(c));
        it->setData(ColName, ColumnRole, c);
        const bool shown = defaults || c == GamesModel::Title || !h->isSectionHidden(c);
        const int w = defaults || h->isSectionHidden(c) ? GamesModel::defaultWidth(c) : h->sectionSize(c);
        it->setText(ColWidth, QString::number(w));
        it->setTextAlignment(ColWidth, Qt::AlignRight | Qt::AlignVCenter);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        it->setCheckState(ColName, shown ? Qt::Checked : Qt::Unchecked);
        if (!shown) for (int k : {ColName, ColWidth}) it->setForeground(k, QBrush(Qt::gray));
    }
    list_->setCurrentItem(list_->topLevelItem(0));
}

void ColumnsEditorDialog::defaultAll() { load(true); }

bool ColumnsEditorDialog::editing() const { // a width being edited
    QWidget* f = QApplication::focusWidget();
    return f && qobject_cast<QLineEdit*>(f) && list_->isAncestorOf(f);
}

QTreeWidgetItem* ColumnsEditorDialog::selected() const { return list_->currentItem(); }

// MoveColumn: Title stays first
void ColumnsEditorDialog::moveSelected(int by) {
    QTreeWidgetItem* it = selected();
    if (!it) return;
    const int r = list_->indexOfTopLevelItem(it), to = r + by;
    if (r == 0 || to < 1 || to >= list_->topLevelItemCount()) { list_->setFocus(); return; }
    const QSignalBlocker block(list_);
    list_->takeTopLevelItem(r);
    list_->insertTopLevelItem(to, it);
    list_->setCurrentItem(it);
    list_->setFocus();
}

void ColumnsEditorDialog::editWidth() {
    if (QTreeWidgetItem* it = selected()) list_->editItem(it, ColWidth);
}

void ColumnsEditorDialog::addWidth(int by) {
    if (QTreeWidgetItem* it = selected()) it->setText(ColWidth, QString::number(std::max(10, it->text(ColWidth).toInt() + by)));
}

void ColumnsEditorDialog::setSelectedWidth(int w) {
    if (QTreeWidgetItem* it = selected()) it->setText(ColWidth, QString::number(std::max(10, w)));
}

void ColumnsEditorDialog::selectColumn(int column) {
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (list_->topLevelItem(i)->data(ColName, ColumnRole).toInt() == column) list_->setCurrentItem(list_->topLevelItem(i));
}

void ColumnsEditorDialog::setShown(int column, bool on) {
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (list_->topLevelItem(i)->data(ColName, ColumnRole).toInt() == column)
            list_->topLevelItem(i)->setCheckState(ColName, on ? Qt::Checked : Qt::Unchecked);
}

QStringList ColumnsEditorDialog::rows() const {
    QStringList out;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = list_->topLevelItem(i);
        out << QString("%1:%2:%3").arg(GamesModel::columnKey(it->data(ColName, ColumnRole).toInt()), it->text(ColWidth),
                                      it->checkState(ColName) == Qt::Checked ? "1" : "0");
    }
    return out;
}

// FormCloseQuery with Ok: the order, widths and visibility to the list
void ColumnsEditorDialog::apply() const {
    QHeaderView* h = in_.header;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* it = list_->topLevelItem(i);
        const int c = it->data(ColName, ColumnRole).toInt();
        const bool shown = c == GamesModel::Title || it->checkState(ColName) == Qt::Checked;
        h->setSectionHidden(c, false); // a hidden section can't be resized
        h->resizeSection(c, std::max(10, it->text(ColWidth).toInt()));
        if (h->visualIndex(c) != i) h->moveSection(h->visualIndex(c), i);
        h->setSectionHidden(c, !shown);
    }
}

void ColumnsEditorDialog::keyPressEvent(QKeyEvent* e) {
    if (editing()) { QDialog::keyPressEvent(e); return; }
    switch (e->key()) {
    case Qt::Key_Return: case Qt::Key_Enter: ui_->ButtonOk->click(); return;
    default: QDialog::keyPressEvent(e);
    }
}

// ColumnsListViewKeyAction
bool ColumnsEditorDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == list_ && e->type() == QEvent::KeyPress && !editing()) {
        auto* k = static_cast<QKeyEvent*>(e);
        const bool ctrl = k->modifiers() == Qt::ControlModifier;
        switch (k->key()) {
        case Qt::Key_Up: if (ctrl) { moveSelected(-1); return true; } break;
        case Qt::Key_Down: if (ctrl) { moveSelected(1); return true; } break;
        case Qt::Key_Left: if (ctrl) addWidth(-1); return true;
        case Qt::Key_Right: if (ctrl) addWidth(1); return true;
        case Qt::Key_F2: editWidth(); return true;
        case Qt::Key_F3: ui_->ButtonDefaultSize->click(); return true;
        case Qt::Key_F4: ui_->ButtonResetSize->click(); return true;
        case Qt::Key_F5: ui_->ButtonReloadProfile->click(); return true;
        case Qt::Key_F6: ui_->ButtonDefault->click(); return true;
        case Qt::Key_Space:
            if (QTreeWidgetItem* it = selected())
                it->setCheckState(ColName, it->checkState(ColName) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            return true;
        default: break;
        }
    }
    return QDialog::eventFilter(o, e);
}

} // namespace eld
