#include "SettingsLayout.h"

#include "VclRuntime.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QListWidget>
#include <QToolButton>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QResizeEvent>
#include <QTimer>
#include <QCoreApplication>
#include <QWidget>

namespace eld::settingsui {

QWidget* take(QWidget* w) {
    if (!w) return w;
    vcl::stopEliding(w);
    if (auto* l = qobject_cast<QLabel*>(w)) l->setMinimumWidth(l->sizeHint().width());
    return w; // (shown once it's in its group: shown now, a widget with no parent would flash as a window)
}

QLabel* hint(const QString& text, QLabel* like, QLabel* reuse) {
    QLabel* l = reuse ? reuse : new QLabel;
    vcl::stopEliding(l);
    if (like && like != l) {
        l->setFont(like->font());
        l->setPalette(like->palette());
        if (like->styleSheet() != l->styleSheet()) l->setStyleSheet(like->styleSheet()); // (each one costs a restyle)
        l->setForegroundRole(like->foregroundRole());
    }
    l->setText(text);
    l->setWordWrap(true);
    l->setMinimumSize(0, 0);
    l->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    l->setTextInteractionFlags(Qt::NoTextInteraction);
    if (l->parentWidget()) l->show();
    return l;
}

QWidget* systemSwitch(QListWidget* list) {
    auto* w = new QWidget;
    w->setObjectName("SystemSwitch");
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto* group = new QButtonGroup(w);
    group->setExclusive(true);
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* it = list->item(i);
        auto* b = new QToolButton;
        b->setObjectName("SystemSwitch" + it->text());
        b->setText(it->text());
        b->setIcon(it->icon());
        b->setIconSize(QSize(24, 24));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setMinimumWidth(110);
        b->setAutoRaise(false);
        if (it->foreground().color() == QColor(Qt::gray))
            b->setToolTip(QCoreApplication::translate("eld::settingsui", "No emulator is set for %1").arg(it->text()));
        group->addButton(b, i);
        h->addWidget(b);
    }
    h->addStretch(1);
    if (QAbstractButton* b = group->button(list->currentRow())) b->setChecked(true);
    QObject::connect(group, &QButtonGroup::idClicked, list, [list](int i) { list->setCurrentRow(i); });
    QObject::connect(list, &QListWidget::currentRowChanged, group, [group](int i) {
        if (QAbstractButton* b = group->button(i)) b->setChecked(true);
    });
    return w;
}

void setPage(QWidget* dialog, QWidget* page) {
    for (QWidget* c : dialog->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        if (!c->isWindow()) c->hide();
    dialog->setMinimumSize(0, 0);
    dialog->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    auto* lay = new QVBoxLayout(dialog);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(page);
    page->show();
    dialog->adjustSize();
}

void refit(QWidget* inPage) {
    // the Settings window fits a tab's page when the tab's room changes: a resize of the room
    // asks for it again, after the page's layout has taken the change in
    QTimer::singleShot(0, inPage, [inPage] {
        for (QWidget* p = inPage; p; p = p->parentWidget())
            if (auto* scroll = qobject_cast<QScrollArea*>(p)) {
                QResizeEvent e(scroll->viewport()->size(), scroll->viewport()->size());
                QCoreApplication::sendEvent(scroll->viewport(), &e);
                return;
            }
    });
}

Group::Group(const QString& title, const QString& name) : box_(new QGroupBox(title)), grid_(new QGridLayout(box_)) {
    if (!name.isEmpty()) box_->setObjectName(name);
    grid_->setContentsMargins(10, 8, 10, 10);
    grid_->setHorizontalSpacing(8);
    grid_->setVerticalSpacing(5);
    grid_->setColumnStretch(1, 1);
}

static constexpr int kAll = 8;

// Shown in its group (not explicitly hidden any more), and so are a layout's widgets.
static void showIn(QLayoutItem* item) {
    if (!item) return;
    if (QWidget* w = item->widget()) w->show();
    if (QLayout* l = item->layout())
        for (int i = 0; i < l->count(); ++i) showIn(l->itemAt(i));
}
static void showAll(QGridLayout* g) {
    for (int i = 0; i < g->count(); ++i) showIn(g->itemAt(i));
} // the columns a line across the group spans

Group& Group::check(QWidget* c, QLabel* explanation) {
    grid_->addWidget(take(c), row_++, 0, 1, kAll);
    if (explanation) {
        auto* indent = new QHBoxLayout;
        indent->setContentsMargins(22, 0, 0, 4);
        indent->addWidget(explanation);
        grid_->addLayout(indent, row_++, 0, 1, kAll);
    }
    showAll(grid_);
    return *this;
}

Group& Group::row(QWidget* label, QWidget* field, std::initializer_list<QWidget*> buttons) {
    if (label) grid_->addWidget(take(label), row_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    if (buttons.size() == 0) {
        grid_->addWidget(take(field), row_, label ? 1 : 0, 1, label ? kAll - 1 : kAll);
    } else {
        auto* h = new QHBoxLayout;
        h->setSpacing(4);
        h->addWidget(take(field), 1);
        for (QWidget* b : buttons) h->addWidget(take(b));
        grid_->addLayout(h, row_, label ? 1 : 0, 1, label ? kAll - 1 : kAll);
    }
    ++row_;
    showAll(grid_);
    return *this;
}

Group& Group::line(std::initializer_list<QWidget*> ws, QWidget* right) {
    auto* h = new QHBoxLayout;
    h->setSpacing(10);
    for (QWidget* w : ws) h->addWidget(take(w));
    h->addStretch(1);
    if (right) h->addWidget(take(right));
    grid_->addLayout(h, row_++, 0, 1, kAll);
    showAll(grid_);
    return *this;
}

Group& Group::wide(QWidget* w) {
    grid_->addWidget(take(w), row_++, 0, 1, kAll);
    showAll(grid_);
    return *this;
}

Group& Group::text(QLabel* l) {
    vcl::stopEliding(l);
    l->setWordWrap(true);
    l->setMinimumSize(0, 0);
    l->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    grid_->addWidget(l, row_++, 0, 1, kAll);
    showAll(grid_);
    return *this;
}

Group& Group::space(int px) {
    grid_->setRowMinimumHeight(row_++, px);
    return *this;
}

Page::Page(const QString& name, const QString& intro, int columns) : page_(new QWidget) {
    page_->setObjectName(name);
    top_ = new QVBoxLayout(page_);
    top_->setContentsMargins(10, 10, 10, 10);
    top_->setSpacing(10);
    if (!intro.isEmpty()) {
        auto* l = new QLabel(intro);
        l->setObjectName(name + "Intro");
        l->setWordWrap(true);
        top_->addWidget(l);
    }
    columns_ = new QHBoxLayout;
    columns_->setSpacing(10);
    top_->addLayout(columns_);
    for (int i = 0; i < columns; ++i) {
        auto* c = new QVBoxLayout;
        c->setSpacing(10);
        columns_->addLayout(c, 1);
        cols_ << c;
    }
    fullRows_ = new QVBoxLayout;
    fullRows_->setSpacing(10);
    top_->addLayout(fullRows_);
}

Page& Page::add(int column, const Group& g) {
    cols_.value(column, cols_.last())->addWidget(g.box());
    return *this;
}

Page& Page::top(const Group& g) {
    top_->insertWidget(top_->indexOf(columns_) >= 0 ? top_->indexOf(columns_) : 1, g.box());
    return *this;
}

Page& Page::full(const Group& g) {
    fullRows_->addWidget(g.box());
    return *this;
}

namespace {
// Something in a page that adapts to the room's width (found by reflow, by its name)
class RoomFit : public QObject {
public:
    explicit RoomFit(QObject* parent) : QObject(parent) { setObjectName("settingsuiColumns"); }
    virtual bool fitTo(int width) = 0;
};

class FnFit : public RoomFit {
public:
    FnFit(QObject* parent, std::function<bool(int)> f) : RoomFit(parent), f_(std::move(f)) {}
    bool fitTo(int width) override { return f_(width); }
private:
    std::function<bool(int)> f_;
};

// A page's columns: side by side when the room is wide enough for them, else stacked
class Columns : public RoomFit {
public:
    Columns(QWidget* page, QVBoxLayout* top, QHBoxLayout* columns, QList<QVBoxLayout*> cols)
        : RoomFit(page), top_(top), columns_(columns), cols_(std::move(cols)) {}
    // the width the columns need side by side
    int sideBySide() const {
        int need = 0;
        for (QVBoxLayout* c : cols_) need += c->minimumSize().width();
        const QMargins m = top_->contentsMargins();
        return need + columns_->spacing() * std::max(0, int(cols_.size()) - 1) + m.left() + m.right();
    }
    bool fitTo(int width) override {
        if (cols_.size() < 2) return false;
        const auto dir = width >= sideBySide() ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (columns_->direction() == dir) return false;
        columns_->setDirection(dir);
        for (int i = 0; i < cols_.size(); ++i) columns_->setStretch(i, dir == QBoxLayout::LeftToRight ? 1 : 0);
        top_->invalidate(); // (its minimum size is cached: the page's width follows)
        return true;
    }
private:
    QVBoxLayout* top_;
    QHBoxLayout* columns_;
    QList<QVBoxLayout*> cols_;
};
} // namespace

bool reflow(QWidget* w, int width) {
    bool changed = false;
    for (QObject* o : w->findChildren<QObject*>("settingsuiColumns"))
        if (auto* c = dynamic_cast<RoomFit*>(o)) changed |= c->fitTo(width);
    return changed;
}

void onRoom(QObject* owner, std::function<bool(int width)> fit) {
    new FnFit(owner, std::move(fit));
}

Page& Page::finish() {
    for (QVBoxLayout* c : cols_) c->addStretch(1);
    top_->addStretch(1);
    new Columns(page_, top_, columns_, cols_);
    page_->adjustSize();
    return *this;
}

} // namespace eld::settingsui
