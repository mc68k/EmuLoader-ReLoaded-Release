// settingsview - development tool: opens the MAME settings window for a data folder, shows one
// page, saves a PNG of it and optionally presses OK (to test what gets written).
//   settingsview <data dir> <out.png> [page 1..8] [game name] [level] [--ok] [--sub 1|2|3] [--reveal Widget] [--set Widget=value]...
// --set: a combo box takes an item text (or #index), an edit box text, a check box 0/1, a list
// selects the row with that text (+text / -text ticks / unticks it).
#include "ElDatabase.h"
#include "Frontend.h"
#include "MachineFilterDialog.h"
#include "MameSettingsDialog.h"
#include "VclRuntime.h"

#include <QAbstractItemModel>
#include "Language.h"
#include <QApplication>
#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTreeView>
#include <QPushButton>
#include <QMetaObject>
#include <QTimer>
#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QStringList a = app.arguments();
    if (a.size() < 3) return 2;
    eld::Frontend fe;
    if (!fe.open(a[1])) return 3;
    // SV_LANG=<file.ini>: a language, with its fonts (Font=, FontSize=), as EmuLoader loads it
    if (qEnvironmentVariableIsSet("SV_LANG")) {
        const QFileInfo lang(qEnvironmentVariable("SV_LANG"));
        eld::installLanguage(lang.completeBaseName(), {lang.absolutePath()}, {});
    }
    // settingsview <data dir> <out.png> machines : the Machines window instead
    if (a.size() > 3 && a[3] == "machines") {
        std::unique_ptr<QDialog> d(new eld::MachineFilterDialog(eld::MachineFilterDialog::load(fe.gamesFiltersFile()),
                                                               fe.resourcesFolder() + "main_icons"));
        QTimer::singleShot(400, [&] { d->grab().save(a[2]); d->done(0); });
        d->exec();
        return 0;
    }
    int page = a.size() > 3 ? a[3].toInt() : 1;
    eld::MameSettingsDialog dlg(fe, eld::SystemId::MAME);
    if (a.size() > 5 && !a[4].isEmpty() && a[4] != "-") {
        eld::ElDatabase db;
        db.load(fe.databaseFile(eld::SystemId::MAME), {});
        const eld::MameGame* g = db.find(a[4]);
        if (!g) return 4;
        dlg.setCustom(*g, a[5].toInt());
    }
    bool ok = a.contains("--ok");
    int sub = a.contains("--sub") ? a[a.indexOf("--sub") + 1].toInt() : 0;
    QString pageButton[] = {"", "ButtonPageFolders", "ButtonPageVideo1", "ButtonPageVideo2", "ButtonPageVideo3",
                            "ButtonPageLUAandAudio", "ButtonPageInput", "ButtonPageMisc", "ButtonPageSDLMAMEInput"};
    QTimer::singleShot(300, [&] {
        if (page >= 1 && page <= 8)
            if (auto* b = dlg.findChild<QPushButton*>(pageButton[page])) b->click();
        for (int i = 0; i + 1 < a.size(); ++i) {
            if (a[i] != "--set") continue;
            QString name = a[i + 1].section('=', 0, 0), v = a[i + 1].section('=', 1);
            QWidget* w = dlg.findChild<QWidget*>(name);
            if (auto* cb = qobject_cast<QComboBox*>(w)) {
                if (v.startsWith('#')) cb->setCurrentIndex(v.mid(1).toInt());
                else if (cb->isEditable()) cb->setCurrentText(v);
                else cb->setCurrentIndex(cb->findText(v));
            } else if (auto* e = qobject_cast<QLineEdit*>(w)) e->setText(v);
            else if (auto* c = qobject_cast<QCheckBox*>(w)) c->setChecked(v == "1");
            else if (auto* t = qobject_cast<QTreeView*>(w)) {
                auto* m = qobject_cast<QStandardItemModel*>(t->model());
                QChar op = v.isEmpty() ? QChar() : v[0];
                QString text = (op == '+' || op == '-') ? v.mid(1) : v;
                for (int r = 0; m && r < m->rowCount(); ++r)
                    for (int c = 0; c < m->columnCount(); ++c)
                        if (m->item(r, c) && m->item(r, c)->text() == text) {
                            if (op == '+' || op == '-') m->item(r, 0)->setCheckState(op == '+' ? Qt::Checked : Qt::Unchecked);
                            else t->setCurrentIndex(m->index(r, 0));
                        }
            } else {
                qWarning("no widget %s", qPrintable(name));
            }
        }
        for (int i = 0; i + 1 < a.size(); ++i)
            if (a[i] == "--click")
                if (auto* b = dlg.findChild<QPushButton*>(a[i + 1])) b->click();
        for (int i = 0; i + 1 < a.size(); ++i) // the page a setting is on, as Find a Setting opens it
            if (a[i] == "--reveal") dlg.reveal(a[i + 1]);
        if (sub >= 1 && sub <= 3) { // Video 3: 1 BGFX, 2 Direct3D, 3 OpenGL
            const char* subs[] = {"", "ButtonPageVideoEffectsBGFX", "ButtonPageVideoEffectsHLSL", "ButtonPageVideoEffectsGLSL"};
            if (auto* b = dlg.findChild<QPushButton*>(subs[sub])) b->click();
        }
        QTimer::singleShot(300, [&] {
            if (qEnvironmentVariableIsSet("SV_DUMP")) for (const QString& n : qEnvironmentVariable("SV_DUMP").split(",")) if (auto* w = dlg.findChild<QWidget*>(n)) qWarning("%s enabled=%d visible=%d", qPrintable(n), w->isEnabled(), w->isVisible());
            dlg.grab().save(a[2]);
            if (ok) QMetaObject::invokeMethod(&dlg, "accept");
            else dlg.reject();
        });
    });
    dlg.exec();
    return 0;
}
