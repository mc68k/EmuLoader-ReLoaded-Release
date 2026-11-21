// SettingsLayout - the Settings window's tabs built with layouts: framed groups in columns that
// fill the window, every control as wide as its whole text, the explanations under the controls
// wrapped in gray. The controls are the original windows' own (their names, code and ini keys),
// moved into these groups.
#pragma once

#include <functional>

#include <QList>
#include <QString>
#include <initializer_list>

class QBoxLayout;
class QObject;
class QGridLayout;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QVBoxLayout;
class QWidget;

namespace eld::settingsui {

// A control moved here: its whole text again (no more "…"), shown.
QWidget* take(QWidget* w);

// An explanation under a control: wrapped, in the gray of the original windows' explanations
// (like: a label of theirs to copy the look of).
QLabel* hint(const QString& text, QLabel* like, QLabel* reuse = nullptr);

// The systems as a row of buttons (MAME | HBMAME), in place of a window's big icon list: the list
// stays (hidden) and keeps the choice; the buttons follow it and set it. A system shown in gray in
// the list (no emulator set) says so in its tooltip.
QWidget* systemSwitch(QListWidget* list);

// A dialog's own content replaced by a page (its old controls hidden): the dialog as a whole tab,
// sized by the page's layout.
void setPage(QWidget* dialog, QWidget* page);

// A part of a page shown or hidden (a folded group opened): the tab fits the page to it again.
void refit(QWidget* inPage);

// The pages inside w for a room width wide: their columns side by side when they fit, else one
// under the other (a tab fits the window instead of the window growing for the widest tab).
// True when a page changed.
bool reflow(QWidget* w, int width);
// fit(width) when the tab's room is width wide (true when it changed something): a part of a page
// that adapts to it (a preview that scales down, two things side by side or one under the other)
void onRoom(QObject* owner, std::function<bool(int width)> fit);

class Group {
public:
    // name: the group's object name (an original box's, so Find a Setting and "open at" still find it)
    Group(const QString& title, const QString& name = {});
    QGroupBox* box() const { return box_; }
    QGridLayout* grid() const { return grid_; }

    // A check box (or radio button) on its own line, its explanation under it.
    Group& check(QWidget* c, QLabel* explanation = nullptr);
    // A label, a field (stretching), and buttons after it.
    Group& row(QWidget* label, QWidget* field, std::initializer_list<QWidget*> buttons = {});
    // Widgets side by side on one line (stretch at the end).
    Group& line(std::initializer_list<QWidget*> ws, QWidget* right = nullptr); // right: at the far right
    // A widget across the whole group.
    Group& wide(QWidget* w);
    // A text across the group (a subheading, a note).
    Group& text(QLabel* l);
    Group& space(int px = 6);

private:
    QGroupBox* box_;
    QGridLayout* grid_;
    int row_ = 0;
};

class Page {
public:
    // intro: one line at the top of the tab saying what it's about
    Page(const QString& name, const QString& intro, int columns);
    QWidget* widget() const { return page_; }
    Page& add(int column, const Group& g);
    Page& full(const Group& g); // across all the columns, below them
    Page& top(const Group& g);  // across all the columns, above them
    Page& finish();             // the stretches at the bottom

private:
    QWidget* page_;
    QVBoxLayout* top_;
    QHBoxLayout* columns_;
    QVBoxLayout* fullRows_;
    QList<QVBoxLayout*> cols_;
};

} // namespace eld::settingsui
