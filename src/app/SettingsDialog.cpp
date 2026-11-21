#include "SettingsDialog.h"

#include "SettingsLayout.h"

#include <QCloseEvent>
#include <QAbstractButton>
#include <QApplication>
#include <QTimer>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QLineEdit>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace eld {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("FormSettings");
    setWindowTitle(tr("Settings"));
    setSizeGripEnabled(true);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("SettingsTabs");
    tabs_->setUsesScrollButtons(true);
    tabs_->setDocumentMode(false);
    lay->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int t) {
        ensureBuilt(t); // (a tab built when shown)
        // and fitted to the room it has now (sized while hidden, it may have missed the last change)
        if (t >= 0 && t < pages_.size() && pages_[t].fit)
            if (auto* scroll = qobject_cast<QScrollArea*>(tabs_->widget(t))) pages_[t].fit(scroll->viewport()->size());
    });
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->setObjectName("SettingsButtons");
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
}

void SettingsDialog::embed(QDialog* d, const QStringList& hide) {
    d->setWindowFlags(Qt::Widget);
    for (const QString& n : hide) {
        if (n == QLatin1String("QDialogButtonBox")) {
            for (QDialogButtonBox* b : d->findChildren<QDialogButtonBox*>()) b->hide();
            continue;
        }
        if (QWidget* w = d->findChild<QWidget*>(n)) w->hide();
    }
}

int SettingsDialog::addDialog(const QString& title, QDialog* d, const QString& okButton, const QString& cancelButton,
                              const QStringList& hide) {
    QStringList all = hide;
    if (!okButton.isEmpty()) all << okButton;
    if (!cancelButton.isEmpty()) all << cancelButton;
    embed(d, all);
    // the tab's own OK or Cancel (a hidden button clicked, or the window closing itself) is the
    // whole window's
    connect(d, &QDialog::finished, this, [this](int r) {
        if (busy_) return;
        r == QDialog::Accepted ? accept() : reject();
    });
    auto ok = [d, okButton]() -> bool {
        if (d->result() == QDialog::Accepted && d->isHidden()) return true; // done already
        if (QAbstractButton* b = okButton.isEmpty() ? nullptr : d->findChild<QAbstractButton*>(okButton)) b->click();
        else d->accept();
        return d->result() == QDialog::Accepted;
    };
    auto cancel = [d, cancelButton] {
        if (QAbstractButton* b = cancelButton.isEmpty() ? nullptr : d->findChild<QAbstractButton*>(cancelButton)) b->click();
        else d->reject();
    };
    return addPage(title, d, ok, cancel);
}

static QScrollArea* newScroll(int n) {
    auto* scroll = new QScrollArea;
    scroll->setObjectName("SettingsPage" + QString::number(n));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return scroll;
}

int SettingsDialog::addLazyPage(const QString& title, std::function<void()> build) {
    Page p;
    p.build = std::move(build);
    pages_.append(p);
    return tabs_->addTab(newScroll(pages_.size() - 1), title);
}

void SettingsDialog::ensureBuilt(int index) {
    if (index < 0 || index >= pages_.size() || !pages_[index].build || filling_ >= 0) return;
    auto build = std::move(pages_[index].build);
    pages_[index].build = nullptr;
    filling_ = index;
    build();
    filling_ = -1;
}

void SettingsDialog::buildAll() {
    for (int i = 0; i < pages_.size(); ++i) ensureBuilt(i);
}

void SettingsDialog::buildRestWhenIdle() {
    // one tab at a time, a little apart, while nothing is pressed: the window answers in between
    auto* t = new QTimer(this);
    t->setInterval(60);
    connect(t, &QTimer::timeout, this, [this, t] {
        if (!isVisible() || done_) return t->deleteLater();
        if (QGuiApplication::mouseButtons() != Qt::NoButton || QApplication::activePopupWidget() ||
            QApplication::activeModalWidget() != this)
            return;
        for (int i = 0; i < pages_.size(); ++i)
            if (pages_[i].build) {
                ensureBuilt(i);
                return;
            }
        t->stop();
        t->deleteLater();
    });
    QTimer::singleShot(250, t, [t] { t->start(); });
}

void SettingsDialog::widenForTabs() {
    QTabBar* bar = tabs_->tabBar();
    int need = bar->sizeHint().width() + 8;
    QWidget* corner = tabs_->cornerWidget(Qt::TopRightCorner);
    if (corner) need += corner->sizeHint().width();
    const QMargins m = layout() ? layout()->contentsMargins() : QMargins();
    need += m.left() + m.right();
    const QRect avail = (screen() ? screen() : QGuiApplication::primaryScreen())->availableGeometry();
    const int most = avail.width() - 40;
    // too wide for the screen (a small screen, or a large screen scaling): the Find a Setting box
    // gives up room, down to about 16 characters, before the tabs need their scroll arrows
    if (need > most && corner)
        if (auto* find = corner->findChild<QLineEdit*>()) {
            const int narrowest = find->fontMetrics().averageCharWidth() * 16;
            const int now = find->minimumWidth();
            const int to = std::max(narrowest, now - (need - most));
            if (to < now) {
                find->setMinimumWidth(to);
                need -= now - to;
                corner->adjustSize();
            }
        }
    const int to = std::min(std::max(width(), need), most);
    if (to != width()) resize(to, height());
}

// the window as wide as the widest tab and as high as the highest, up to 900 pixels high and the
// screen (a bigger tab scrolls)
void SettingsDialog::growFor(QWidget* w) {
    // at least about 120 characters wide (two columns of settings side by side, in this font)
    const int base = fontMetrics().averageCharWidth() * 120;
    const QSize want = QSize(std::max(w->width() + 40, base), w->height() + 110).boundedTo(QSize(QWIDGETSIZE_MAX, 900));
    const QRect avail = (screen() ? screen() : QGuiApplication::primaryScreen())->availableGeometry();
    const QSize to(std::min(std::max(width(), want.width()), avail.width() - 40),
                   std::min(std::max(height(), want.height()), avail.height() - 40));
    if (to != size()) resize(to);
}

int SettingsDialog::addPage(const QString& title, QWidget* w, std::function<bool()> ok, std::function<void()> cancel) {
    int i;
    QScrollArea* scroll;
    if (filling_ >= 0) { // a tab built when first shown: its place is there already
        i = filling_;
        scroll = qobject_cast<QScrollArea*>(tabs_->widget(i));
        pages_[i].widget = w;
        pages_[i].ok = std::move(ok);
        pages_[i].cancel = std::move(cancel);
        scroll->setWidget(w);
        w->show();
    } else {
        scroll = newScroll(pages_.size());
        scroll->setWidget(w);
        w->show();
        Page p;
        p.widget = w;
        p.ok = std::move(ok);
        p.cancel = std::move(cancel);
        pages_.append(p);
        i = tabs_->addTab(scroll, title);
    }
    // while setting up, the window takes the size of the tabs built then (a tab built later fits
    // the window: its columns stack when they don't fit side by side)
    if (!isVisible()) growFor(w);
    // a tab made with a layout fills the room it has (and scrolls only when it needs more)
    if (QLayout* l = w->layout())
        setPageFit(i, [w, l](const QSize& room) {
            if (settingsui::reflow(w, room.width())) {
                l->invalidate(); // (the sizes it had cached)
                l->activate();
            }
            const QSize min = l->totalMinimumSize();
            const int width = std::max(min.width(), room.width());
            const int height = std::max(l->hasHeightForWidth() ? l->totalHeightForWidth(width) : min.height(), room.height());
            if (w->size() != QSize(width, height)) w->resize(width, height);
        });
    return i;
}

void SettingsDialog::setPageFit(int index, std::function<void(const QSize&)> fit) {
    if (index < 0 || index >= pages_.size()) return;
    pages_[index].fit = std::move(fit);
    if (auto* scroll = qobject_cast<QScrollArea*>(tabs_->widget(index))) {
        scroll->viewport()->installEventFilter(this);
        pages_[index].fit(scroll->viewport()->size());
    }
}

bool SettingsDialog::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Resize)
        for (int i = 0; i < pages_.size(); ++i)
            if (auto* scroll = qobject_cast<QScrollArea*>(tabs_->widget(i)))
                if (scroll->viewport() == o && pages_[i].fit) {
                    pages_[i].fit(scroll->viewport()->size());
                    break;
                }
    return QDialog::eventFilter(o, e);
}

void SettingsDialog::arrange(const QList<int>& order) {
    if (order.size() != pages_.size()) return;
    QVector<Page> pages;
    QList<QWidget*> scrolls;
    QStringList titles;
    for (int old : order) {
        pages << pages_.value(old);
        scrolls << tabs_->widget(old);
        titles << tabs_->tabText(old);
    }
    const bool was = tabs_->blockSignals(true);
    while (tabs_->count()) tabs_->removeTab(0);
    pages_ = pages;
    for (int i = 0; i < scrolls.size(); ++i) tabs_->addTab(scrolls[i], titles[i]);
    tabs_->blockSignals(was);
}

void SettingsDialog::setCurrentPage(int index) {
    ensureBuilt(index);
    if (index >= 0 && index < tabs_->count()) tabs_->setCurrentIndex(index);
}

void SettingsDialog::showPageOf(QWidget* inside) {
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].widget && (pages_[i].widget == inside || pages_[i].widget->isAncestorOf(inside))) {
            tabs_->setCurrentIndex(i);
            return;
        }
}

int SettingsDialog::pageIndex(const QString& title) const {
    for (int i = 0; i < tabs_->count(); ++i)
        if (tabs_->tabText(i) == title) return i;
    return -1;
}

void SettingsDialog::accept() {
    if (busy_ || done_) return;
    busy_ = true;
    for (int i = 0; i < pages_.size(); ++i)
        if (pages_[i].ok && !pages_[i].ok()) { // refused: stay, at that tab
            tabs_->setCurrentIndex(i);
            busy_ = false;
            return;
        }
    done_ = true;
    busy_ = false;
    QDialog::accept();
}

void SettingsDialog::reject() {
    if (busy_) return;
    busy_ = true;
    if (!done_)
        for (const Page& p : pages_)
            if (p.cancel) p.cancel();
    done_ = true;
    busy_ = false;
    QDialog::reject();
}

void SettingsDialog::closeEvent(QCloseEvent* e) {
    reject();
    e->accept();
}

} // namespace eld
