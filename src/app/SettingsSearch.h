// SettingsSearch - "Find a Setting": searches the settings of every window and menu by name and
// tooltip, lists where each one lives, and "Take me there" opens the window at the right page
// with the setting highlighted (or opens the menu with the item selected).
//
// Nothing is listed by hand: the entries are collected from the windows' controls (check boxes,
// radio buttons, labels, group boxes, with their tooltips and the gray description labels under
// them) and from the menus, in the language in use. New options show up by themselves.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QLineEdit;
class QMenu;
class QPushButton;
class QTreeWidget;

namespace eld {

struct SettingEntry {
    QString text;      // the setting's name as shown
    QString extra;     // tooltip and description, searched too
    QString where;     // "Preferences › PAGE 1 › General"
    QString window;    // "preferences", "mame", "menu"
    QString target;    // object name of the control (or "<menu>/<action>" for menus)
    bool available = true; // false: grayed out (not ported yet, or not in this MAME version)
};

namespace SettingsSearch {

// The settings of a window: its check boxes, radio buttons, labels and group boxes.
// where(w) gives the path inside the window (page, group).
void addWindow(QList<SettingEntry>& out, QWidget* root, const QString& window, const QString& title,
               const std::function<QStringList(QWidget*)>& where);

// The items of a menu and its submenus, the submenus too. Items whose hint is a web address are
// left out.
void addMenu(QList<SettingEntry>& out, QMenu* menu, const QString& menuName, const QString& title);

// Entries matching every word of the query, best first: names starting with it, then names with
// a word starting with it, names containing it, the ones that match by tooltip, and last the ones
// that match only by where they are.
// Case and accents are ignored.
QList<SettingEntry> search(const QList<SettingEntry>& entries, const QString& query);

// The nearest group box title above a control, for where().
QString groupOf(QWidget* w, QWidget* stop);

// Flashes a frame around a control (after its window has been shown) and gives it the focus.
void highlight(QWidget* w);

// Opens a menu at a point with its submenus down to the action, the action selected and
// flashing as highlight() does.
// For about a second after the item is shown, a click outside the menus is ignored (a click made as
// the menu appears). When the menus close later without an item chosen and without Esc (a click
// elsewhere), closedByClick is called, so Find a Setting can come back.
void revealInMenu(QMenu* top, QAction* action, const QPoint& globalPos,
                  std::function<void()> closedByClick = {});

} // namespace SettingsSearch

// The search window: a box to type in, the matches with where they are, and "Take me there".
class FindSettingDialog : public QDialog {
    Q_OBJECT
public:
    FindSettingDialog(QList<SettingEntry> entries, const QString& query, QWidget* parent = nullptr);
    // The entry chosen with "Take me there" (or a double-click), after exec() returns Accepted.
    SettingEntry chosen() const { return chosen_; }
    // The search and the match that was chosen, to open the window again as it was.
    QString query() const;
    int chosenRow() const { return chosenRow_; }
    void selectRow(int row);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void update();
    void take();
    QList<SettingEntry> entries_, shown_;
    SettingEntry chosen_;
    int chosenRow_ = -1;
    QLineEdit* edit_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* take_ = nullptr;
};

} // namespace eld
