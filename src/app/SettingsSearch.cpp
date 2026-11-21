#include "SettingsSearch.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QTextDocument>
#include <QCursor>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace eld {

namespace {

const QString kSep = QString::fromUtf8(" › "); // " › "

// What a control says: no keyboard shortcut marks, one line.
QString clean(QString t) {
    t.replace("&&", "\x01").remove('&').replace("\x01", "&");
    t.replace('\n', ' ');
    return t.simplified();
}

// A text worth searching: it has a word of two letters or more (not "[1.00]" or "%").
bool wordy(const QString& t) {
    static const QRegularExpression word("\\p{L}{2,}");
    return word.match(t).hasMatch();
}

// Lower case without accents, for matching ("Émulateurs" ~ "emulateurs").
QString fold(const QString& s) {
    QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
    return out;
}

QString ownTip(QWidget* w) {
    // elideLongTexts puts the whole text in front of the control's own tooltip.
    return w->property("elTip").toBool() ? w->property("elOwnTip").toString() : w->toolTip();
}

QString controlText(QWidget* w) {
    if (auto* b = qobject_cast<QAbstractButton*>(w)) {
        QVariant full = w->property("elFull"); // elideLongTexts keeps the whole text
        return clean(full.isValid() && w->property("elShown").toString() == b->text() ? full.toString() : b->text());
    }
    if (auto* l = qobject_cast<QLabel*>(w)) {
        if (!l->pixmap().isNull()) return {};
        QString t = l->text();
        QVariant full = w->property("elFull");
        if (full.isValid() && w->property("elShown").toString() == t) t = full.toString();
        if (l->textFormat() == Qt::RichText || (l->textFormat() == Qt::AutoText && Qt::mightBeRichText(t))) {
            QTextDocument doc;
            doc.setHtml(t);
            t = doc.toPlainText();
        }
        return clean(t);
    }
    if (auto* g = qobject_cast<QGroupBox*>(w)) return clean(g->title());
    return {};
}

} // namespace

namespace SettingsSearch {

QString groupOf(QWidget* w, QWidget* stop) {
    for (QWidget* p = w->parentWidget(); p && p != stop; p = p->parentWidget()) {
        if (auto* g = qobject_cast<QGroupBox*>(p); g && !clean(g->title()).isEmpty()) return clean(g->title());
        // A panel named "...Box" (TPanelEx) has its heading as the label at its top.
        if (p->objectName().endsWith("Box")) {
            QLabel* heading = nullptr;
            for (QObject* o : p->children())
                if (auto* l = qobject_cast<QLabel*>(o); l && l->y() < 10 && (!heading || l->y() < heading->y()))
                    heading = l;
            if (heading && heading != w && wordy(controlText(heading))) return controlText(heading);
        }
    }
    return {};
}

void addWindow(QList<SettingEntry>& out, QWidget* root, const QString& window, const QString& title,
               const std::function<QStringList(QWidget*)>& where) {
    // A label named "<Control>Label" describes <Control>: its text is searched with the control,
    // or is the control's name when the control has no text of its own (a list, a slider).
    QHash<QWidget*, QString> described;
    QSet<QWidget*> merged;
    for (QLabel* l : root->findChildren<QLabel*>()) {
        QString n = l->objectName();
        if (!n.endsWith("Label") || n.size() <= 5) continue;
        auto* c = root->findChild<QWidget*>(n.chopped(5));
        if (!c || qobject_cast<QLabel*>(c)) continue;
        QString t = controlText(l);
        if (t.isEmpty()) continue;
        described[c] += (described[c].isEmpty() ? "" : " ") + t;
        merged.insert(l);
    }
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (w->objectName().isEmpty() || merged.contains(w) || w->property("searchSkip").toBool()) continue;
        bool kind = qobject_cast<QCheckBox*>(w) || qobject_cast<QRadioButton*>(w) || qobject_cast<QLabel*>(w) ||
                    qobject_cast<QGroupBox*>(w) || described.contains(w);
        if (!kind) continue;
        QString text = controlText(w);
        QString extra = clean(ownTip(w));
        if (described.contains(w)) {
            if (text.isEmpty()) text = described.value(w);
            else extra = (extra + " " + described.value(w)).trimmed();
        }
        if (text.isEmpty() || !wordy(text)) continue;
        SettingEntry e;
        e.text = text;
        e.extra = extra == text ? QString() : extra;
        e.where = (QStringList{title} + where(w)).join(kSep);
        e.window = window;
        e.target = w->objectName();
        e.available = w->isEnabled();
        out.append(e);
    }
}

void addMenu(QList<SettingEntry>& out, QMenu* menu, const QString& menuName, const QString& title) {
    std::function<void(QMenu*, const QStringList&)> walk = [&](QMenu* m, const QStringList& path) {
        for (QAction* a : m->actions()) {
            if (a->isSeparator() || !a->isVisible() || a->objectName().isEmpty()) continue;
            QString text = clean(a->text());
            if (a->property("hint").toString().startsWith("http", Qt::CaseInsensitive) || !wordy(text)) continue;
            SettingEntry e;
            e.text = text;
            QString tip = clean(a->toolTip());
            e.extra = tip == text ? QString() : tip;
            e.where = path.join(kSep);
            e.window = "menu";
            e.target = menuName + "/" + a->objectName();
            e.available = a->isEnabled();
            out.append(e); // a submenu is listed too ("Icon Size"), then its items
            if (a->menu()) walk(a->menu(), path + QStringList{text});
        }
    };
    walk(menu, {title});
}

QList<SettingEntry> search(const QList<SettingEntry>& entries, const QString& query) {
    const QStringList words = fold(query).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty()) return {};
    struct Hit { int rank; const SettingEntry* e; };
    QList<Hit> hits;
    QSet<QString> seen;
    for (const SettingEntry& e : entries) {
        // The name and tooltip first; then where it is, so "icon size" also finds the items of the
        // Icon Size menu and "video 3" the settings on that page. Not the window or top menu
        // ("Preferences", "Menu"): those words would list everything in it.
        QString name = fold(e.text), all = name + " " + fold(e.extra),
                place = all + " " + fold(e.where.section(kSep, 1));
        auto has = [&](const QString& text) {
            return std::all_of(words.begin(), words.end(), [&](const QString& w) { return text.contains(w); });
        };
        if (!has(place)) continue;
        QString key = e.window + "|" + e.text + "|" + e.where; // the same text twice in one place
        if (seen.contains(key)) continue;
        seen.insert(key);
        int rank = has(all) ? 3 : 4;
        QString q = words.join(' ');
        if (name.startsWith(q)) rank = 0;
        else if (std::all_of(words.begin(), words.end(), [&](const QString& w) {
                     return QRegularExpression("(^|\\W)" + QRegularExpression::escape(w)).match(name).hasMatch(); }))
            rank = 1;
        else if (std::all_of(words.begin(), words.end(), [&](const QString& w) { return name.contains(w); }))
            rank = 2;
        if (!e.available) rank += 5; // grayed out ones after the ones that work
        hits.append({rank, &e});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        // Found by where they are: grouped by place, in their order there (menu order).
        if (a.rank % 5 == 4) return a.e->where.localeAwareCompare(b.e->where) < 0;
        return a.e->text.localeAwareCompare(b.e->text) < 0;
    });
    QList<SettingEntry> out;
    for (const Hit& h : hits) out.append(*h.e);
    return out;
}

// The red frame with a yellow tint over r in parent, flashing three times. It stays shown and only
// its colors blink: a widget appearing and going inside a menu makes Windows send the menu mouse
// moves, and a menu that thinks the mouse moved closes the submenu it just opened.
static void flashFrame(QWidget* parent, const QRect& r) {
    static const char* on = "QFrame { border: 3px solid #e81123; border-radius: 4px; background: rgba(255, 210, 0, 70); }";
    static const char* off = "QFrame { border: 3px solid transparent; background: transparent; }";
    auto* frame = new QFrame(parent);
    frame->setAttribute(Qt::WA_TransparentForMouseEvents);
    frame->setStyleSheet(on);
    frame->setGeometry(r);
    frame->show();
    frame->raise();
    auto* t = new QTimer(frame);
    QObject::connect(t, &QTimer::timeout, frame, [frame, t] {
        int n = frame->property("flashes").toInt() + 1;
        frame->setProperty("flashes", n);
        if (n >= 7) {
            t->stop();
            frame->setStyleSheet(off);
            frame->deleteLater();
            return;
        }
        frame->setStyleSheet(n % 2 == 0 ? on : off);
    });
    t->start(350);
}

void highlight(QWidget* w) {
    if (!w) return;
    QPointer<QWidget> target(w);
    QTimer::singleShot(120, w, [target] {
        if (!target || !target->window()->isVisible()) return;
        QWidget* win = target->window();
        // Scroll it into view when it's in a scroll area.
        for (QWidget* p = target->parentWidget(); p; p = p->parentWidget())
            if (auto* sa = qobject_cast<QScrollArea*>(p)) sa->ensureWidgetVisible(target);
        QRect r(target->mapTo(win, QPoint(0, 0)), target->size());
        flashFrame(win, r.adjusted(-4, -3, 4, 3));
        if (target->focusPolicy() != Qt::NoFocus) target->setFocus(Qt::OtherFocusReason);
    });
}

namespace {

// Watches the menus Find a Setting opened: ignores a click outside them for a moment after the
// item is shown, and tells how they closed (an item chosen, Esc, or a click elsewhere).
class RevealWatch : public QObject {
public:
    RevealWatch(QMenu* top, QList<QMenu*> chain, std::function<void()> closedByClick)
        : QObject(top), chain_(std::move(chain)), closedByClick_(std::move(closedByClick)) {
        for (QMenu* m : chain_) m->installEventFilter(this);
        // Listened to from the next turn of the event loop: Find a Setting itself may have been
        // started from this menu (Ctrl+Shift+F), whose triggered() comes after it returns.
        QTimer::singleShot(0, this, [this] {
            for (QMenu* m : chain_) connect(m, &QMenu::triggered, this, [this] { chosen_ = true; });
        });
        connect(top, &QMenu::aboutToHide, this, [this] {
            // An item's triggered() comes after the menus hide: decide once that's done.
            QTimer::singleShot(0, this, [this] {
                for (QMenu* m : chain_) m->removeEventFilter(this);
                if (!chosen_ && !escaped_ && closedByClick_) closedByClick_();
                deleteLater();
            });
        });
    }
    void startGrace() { grace_.start(); }

protected:
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) escaped_ = true;
        if (e->type() == QEvent::MouseButtonPress && grace_.isValid() && grace_.elapsed() < 1000) {
            QPoint at = static_cast<QMouseEvent*>(e)->globalPosition().toPoint();
            bool inside = false;
            for (QMenu* m : chain_) inside |= m->isVisible() && m->geometry().contains(at);
            if (!inside) return true; // a click as the menu appears: the menu stays
        }
        return false;
    }

private:
    QList<QMenu*> chain_;
    std::function<void()> closedByClick_;
    bool chosen_ = false, escaped_ = false;
    QElapsedTimer grace_;
};

} // namespace

void revealInMenu(QMenu* top, QAction* action, const QPoint& globalPos, std::function<void()> closedByClick) {
    // The chain of menus from top down to the action.
    QList<QMenu*> chain;
    std::function<bool(QMenu*)> find = [&](QMenu* m) {
        chain.append(m);
        for (QAction* a : m->actions()) {
            if (a == action) return true;
            if (a->menu() && find(a->menu())) return true;
        }
        chain.removeLast();
        return false;
    };
    if (!find(top)) return;
    QList<QMenu*> menus = chain;
    if (action->menu()) menus << action->menu();
    QPointer<RevealWatch> watch = new RevealWatch(top, menus, std::move(closedByClick));
    top->popup(globalPos);
    // Each submenu is opened by the menu above it, as the Right arrow key does when its item is
    // selected: the menus are then one chain, and a click elsewhere closes them all (a submenu
    // opened on its own with popup() would close alone and leave the menus above it open).
    auto openSub = [](QMenu* m, QAction* a) {
        m->setActiveAction(a);
        QKeyEvent press(QEvent::KeyPress, m->layoutDirection() == Qt::RightToLeft ? Qt::Key_Left : Qt::Key_Right,
                        Qt::NoModifier);
        QCoreApplication::sendEvent(m, &press);
    };
    auto openNext = std::make_shared<std::function<void(int)>>();
    *openNext = [chain, action, openNext, openSub, watch](int i) {
        QMenu* m = chain[i];
        if (!m->isVisible()) return; // closed meanwhile
        if (i + 1 >= chain.size()) {
            m->setActiveAction(action);
            QRect g = m->actionGeometry(action);
            // The mouse pointer goes there too: where it was (the Take me there button) may be over
            // one of these menus, and its first move would select another item there and close
            // the menus under it. From the item, moving the mouse works as in any menu.
            QCursor::setPos(m->mapToGlobal(QPoint(g.left() + std::min(40, g.width() / 3), g.center().y())));
            flashFrame(m, g.adjusted(1, 0, -1, 0)); // inside the menu, over the item
            if (watch) watch->startGrace();
            // a submenu: opened once the flashing is over (the menus have settled by then)
            if (action->menu())
                QTimer::singleShot(2300, m, [m, action, openSub] {
                    if (m->isVisible() && (!m->activeAction() || m->activeAction() == action)) openSub(m, action);
                });
            return;
        }
        openSub(m, chain[i + 1]->menuAction());
        QTimer::singleShot(80, chain[i + 1], [openNext, i] { (*openNext)(i + 1); });
    };
    QTimer::singleShot(80, top, [openNext] { (*openNext)(0); });
}

} // namespace SettingsSearch

// ------------------------------------------------------------------ window

FindSettingDialog::FindSettingDialog(QList<SettingEntry> entries, const QString& query, QWidget* parent)
    : QDialog(parent), entries_(std::move(entries)) {
    setWindowTitle(tr("Find a Setting"));
    auto* lay = new QVBoxLayout(this);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(tr("Type part of a setting's name, for example \"minimize\" or \"snapshot\""));
    edit_->setClearButtonEnabled(true);
    lay->addWidget(edit_);
    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderLabels({tr("Setting"), tr("Where")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setAlternatingRowColors(true);
    list_->header()->setStretchLastSection(true);
    list_->setColumnWidth(0, 330);
    lay->addWidget(list_, 1);
    auto* note = new QLabel(tr("Grayed out: not ported yet, or not available in your MAME version."), this);
    note->setEnabled(false);
    lay->addWidget(note);
    auto* buttons = new QDialogButtonBox(this);
    take_ = buttons->addButton(tr("Take me there"), QDialogButtonBox::AcceptRole);
    take_->setDefault(true);
    buttons->addButton(QDialogButtonBox::Close);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &FindSettingDialog::take);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(edit_, &QLineEdit::textChanged, this, &FindSettingDialog::update);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &FindSettingDialog::take);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] { take_->setEnabled(list_->currentItem()); });
    // Up and Down in the box move through the matches; Enter takes you there.
    edit_->installEventFilter(this);
    connect(edit_, &QLineEdit::returnPressed, this, &FindSettingDialog::take);
    resize(860, 480);
    edit_->setText(query);
    update();
}

void FindSettingDialog::update() {
    shown_ = SettingsSearch::search(entries_, edit_->text());
    list_->clear();
    for (const SettingEntry& e : shown_) {
        auto* it = new QTreeWidgetItem(list_, {e.text, e.where});
        QString tip = e.text + (e.extra.isEmpty() ? QString() : "\n\n" + e.extra);
        it->setToolTip(0, tip);
        it->setToolTip(1, e.where);
        if (!e.available) {
            QColor gray = palette().color(QPalette::Disabled, QPalette::Text);
            it->setForeground(0, gray);
            it->setForeground(1, gray);
        }
    }
    if (list_->topLevelItemCount()) list_->setCurrentItem(list_->topLevelItem(0));
    take_->setEnabled(list_->currentItem());
}

bool FindSettingDialog::eventFilter(QObject* o, QEvent* e) {
    if (o == edit_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down || k->key() == Qt::Key_PageUp || k->key() == Qt::Key_PageDown) {
            QApplication::sendEvent(list_, e);
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

QString FindSettingDialog::query() const { return edit_->text(); }

void FindSettingDialog::selectRow(int row) {
    if (QTreeWidgetItem* it = list_->topLevelItem(row)) {
        list_->setCurrentItem(it);
        list_->scrollToItem(it);
    }
}

void FindSettingDialog::take() {
    int row = list_->currentItem() ? list_->indexOfTopLevelItem(list_->currentItem()) : -1;
    if (row < 0 || row >= shown_.size()) return;
    chosen_ = shown_[row];
    chosenRow_ = row;
    accept();
}

} // namespace eld
