// Menus - builds QMenus from the <Form>.menus.json files written by dfm2ui.py,
// so menus keep the original EmuLoader captions, order, groups and names.
// Handlers are attached by menu item name; items nobody handles are disabled
// with a "not ported yet" tooltip.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QMenu>
#include <QString>
#include <functional>

namespace eld {

class Menus {
public:
    bool load(const QString& jsonPath, QWidget* parent);
    QMenu* menu(const QString& name) const { return menus_.value(name); }
    QAction* action(const QString& name) const { return actions_.value(name); }

    void on(const QString& itemName, std::function<void()> handler);
    // Call once all handlers are attached: disables everything left unhandled.
    void disableUnhandled(const QStringList& hide = {});

private:
    void build(QMenu* menu, const QJsonArray& items);
    QHash<QString, QMenu*> menus_;
    QHash<QString, QAction*> actions_;
    QHash<QString, bool> handled_;
};

} // namespace eld
