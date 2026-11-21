#include "Menus.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace eld {

bool Menus::load(const QString& jsonPath, QWidget* parent) {
    QFile f(jsonPath);
    if (!f.open(QFile::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        auto* m = new QMenu(parent);
        m->setObjectName(it.key());
        m->setToolTipsVisible(true);
        build(m, it.value().toArray());
        menus_.insert(it.key(), m);
    }
    return true;
}

void Menus::build(QMenu* menu, const QJsonArray& items) {
    QHash<int, QActionGroup*> groups;
    for (const auto& v : items) {
        QJsonObject o = v.toObject();
        // Captions and hints go through the language file ([Menus] in lang\<Name>.ini).
        QString caption = QCoreApplication::translate("Menus", o["caption"].toString().toUtf8().constData());
        if (caption == "-") {
            QAction* sep = menu->addSeparator();
            sep->setObjectName(o["name"].toString());
            sep->setVisible(o["visible"].toBool(true));
            continue;
        }
        QJsonArray sub = o["items"].toArray();
        QAction* a;
        if (!sub.isEmpty()) {
            QMenu* sm = menu->addMenu(caption);
            sm->setToolTipsVisible(true);
            sm->setObjectName(o["name"].toString());
            build(sm, sub);
            a = sm->menuAction();
        } else {
            a = menu->addAction(caption);
            bool checkable = o["checked"].toBool() || o["autoCheck"].toBool() || o["radio"].toBool();
            a->setCheckable(checkable);
            a->setChecked(o["checked"].toBool());
            if (o["radio"].toBool()) {
                int g = o["group"].toInt();
                if (!groups.contains(g)) groups[g] = new QActionGroup(menu);
                groups[g]->addAction(a);
            }
        }
        a->setObjectName(o["name"].toString());
        QString hint = o["hint"].toString();
        a->setProperty("hint", hint); // the original's hint (a web address for the Help links)
        bool url = hint.startsWith("http", Qt::CaseInsensitive);
        a->setToolTip(url || hint.isEmpty() ? hint : QCoreApplication::translate("Menus", hint.toUtf8().constData()));
        a->setVisible(o["visible"].toBool(true));
        a->setData(o["tag"].toInt());
        actions_.insert(o["name"].toString(), a);
    }
}

void Menus::on(const QString& itemName, std::function<void()> handler) {
    QAction* a = actions_.value(itemName);
    if (!a) return;
    QObject::connect(a, &QAction::triggered, a, [handler] { handler(); });
    handled_[itemName] = true;
    a->setEnabled(true); // may be attached after disableUnhandled()
    if (a->property("notPorted").toBool()) {
        a->setProperty("notPorted", false);
        a->setToolTip({});
    }
}

void Menus::disableUnhandled(const QStringList& hide) {
    for (auto it = actions_.begin(); it != actions_.end(); ++it) {
        QAction* a = it.value();
        if (hide.contains(it.key())) { a->setVisible(false); continue; }
        if (a->menu() || a->isSeparator() || handled_.value(it.key())) continue;
        a->setEnabled(false);
        a->setToolTip(QCoreApplication::translate("Menus", "Not ported to the C++ version yet"));
        a->setProperty("notPorted", true);
    }
    // Hide submenus whose items are all hidden.
    for (QMenu* m : menus_)
        for (QAction* a : m->actions())
            if (a->menu()) {
                bool any = false;
                for (QAction* c : a->menu()->actions()) any |= c->isVisible() && !c->isSeparator();
                if (!any) a->setVisible(false);
            }
}

} // namespace eld
