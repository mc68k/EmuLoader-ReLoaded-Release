// ToolBarCustomizer - Settings > Tool Bar: the tool bar's buttons and their order. On top, the
// tool bar as it is; below, the buttons not on it, a separator and a space. Drag a button to move
// it, onto the tool bar to add it, off it to take it away (or select one and use the buttons, for
// the keyboard). Presets: Default (the original's buttons, then the Guide at the right end),
// Original (8.9.4's order) and arrangements saved by name (EmuLoaderRL\toolbar_presets.ini).
// The options: Show Tool Bar, Bound to Games Panel, the search box, the icon size. Every change
// shows in the main window at once (as the original's Customize Tool Bar).
#pragma once

#include <QIcon>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QListWidgetItem;

namespace eld {

class ToolBarCustomizer : public QWidget {
    Q_OBJECT
public:
    struct Tool {
        QString id;            // the button's name ("ButtonGameFilterFavorites")
        QString caption;       // as on the tool bar ("Favorites")
        QString title, detail; // "Favorites Filter", "MAME and Arcade"
        QIcon icon;
    };
    struct Input {
        QVector<Tool> tools;   // every button there is
        QStringList order;     // the tool bar now: names, "|" (separator), "space"
        QStringList original;  // 8.9.4's order
        QStringList defaults;  // this version's: the original's, then a space and the Guide
        QString presetsFile;
        bool showBar = true, bound = false, showSearch = true;
        int iconSize = 1;      // 0 68x68, 1 48x48, 2 30x24
        std::function<void(const QStringList&)> setOrder;
        std::function<void(bool)> setShowBar, setBound, setShowSearch;
        std::function<void(int)> setIconSize;
    };
    explicit ToolBarCustomizer(const Input& in, QWidget* parent = nullptr);

    QStringList order() const;
    void setOrder(const QStringList& order); // shown here and on the tool bar
    void add(const QString& token, int at = -1);
    void remove(int index);
    void move(int index, int delta);
    // "Default", "Original" or a saved name
    void usePreset(const QString& name);
    QStringList savedPresets() const;
    void savePreset(const QString& name);
    void deletePreset(const QString& name);

    QListWidget* bar() const { return bar_; }
    QListWidget* palette() const { return palette_; }

protected:
    bool eventFilter(QObject* o, QEvent* e) override; // dragging a button (press, move, release)

private:
    // Dragging is done here with the mouse, not the system's drag and drop (which couldn't drop
    // between buttons of these lists): a picture of the button follows the mouse, a line shows
    // where it goes on the tool bar.
    int gapAt(const QPoint& pos) const; // where a button dropped at pos (the tool bar's) goes
    void dragTo(const QPoint& global);
    void dropAt(const QPoint& global);
    void endDrag();
    QListWidget* pressedIn_ = nullptr;
    QListWidgetItem* pressedItem_ = nullptr;
    QPoint pressedAt_;
    bool dragging_ = false;
    QLabel* ghost_ = nullptr;   // the button's picture under the mouse
    QFrame* marker_ = nullptr;  // where it goes on the tool bar
    QListWidgetItem* makeItem(const QString& token) const;
    void fillBar(const QStringList& order);
    void fillPalette();
    void fillPresets(const QString& current = {});
    void barChanged();

    Input in_;
    QListWidget* bar_ = nullptr;
    QListWidget* palette_ = nullptr;
    QComboBox* presets_ = nullptr;
    bool filling_ = false;
    bool pending_ = false;
};

} // namespace eld
