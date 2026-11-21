// MainWindow: the games list when it shows no games. A message in the list itself says why and
// offers what's needed: no emulator set up (Set Up MAME...), no games list yet (Create the Games
// List), a search that finds nothing (Clear the Search), or filters that leave nothing (they're
// named; Clear the Filters). Shown and hidden by updateStatus, after every change of the list.
#include "MainWindow.h"
#include "ui_FormMain.h"

#include "DiagnosticLog.h"

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace eld {

namespace {
// Keeps the message over the whole of the list's inside as it's resized
class FillParent : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Resize)
            if (auto* w = qobject_cast<QWidget*>(parent())) w->setGeometry(static_cast<QWidget*>(o)->rect());
        return false;
    }
};
} // namespace

// The filters in use, in words ("Favorites only", "Main CPU: Z80" ...)
QStringList MainWindow::filtersInUse() const {
    QStringList out;
    auto caption = [this](const char* action) {
        QAction* a = menus_.action(action);
        return a ? a->text().remove('&') : QString();
    };
    if (filter_->favoritesOnly()) out << tr("Favorites only");
    const char* hm[] = {"PopupShowHaveMiss", "PopupShowHave", "PopupShowMiss", "PopupShowMissROMs",
                        "PopupShowHaveAndMissROMs", "PopupShowMissAndMissROMs"};
    if (filter_->haveMiss() > 0 && filter_->haveMiss() < 6) out << caption(hm[filter_->haveMiss()]);
    const char* pc[] = {"PopupShowParentClone", "PopupShowParent", "PopupShowClone"};
    if (filter_->parentClone() > 0 && filter_->parentClone() < 3) out << caption(pc[filter_->parentClone()]);
    const QStringList driver = {QString(), tr("Good + Imperfect"), tr("Good"), tr("Imperfect"), tr("Preliminary")};
    if (filter_->driver() > 0 && filter_->driver() < driver.size()) out << tr("Driver status: %1").arg(driver[filter_->driver()]);
    if (!filter_->favoritesOnly() && filter_->arcadeFilter() != "allgames")
        out << tr("Games Filters: %1").arg(arcadeFilterTitle(filter_->arcadeFilter()));
    if (!filter_->cpuFilter().isEmpty()) out << tr("Main CPU: %1").arg(filter_->cpuFilter());
    MiscFilters misc = filter_->miscFilters();
    misc.mainCpu = false; // (the CPU row shown: not a filter by itself)
    if (!misc.isDefault()) out << tr("Miscellaneous Filters");
    out.removeAll(QString());
    return out;
}

// Clear the Filters: every filter back to showing all games (the CPU row stays shown)
void MainWindow::clearAllFilters() {
    DiagnosticLog::write("filters", "Clear the Filters (the empty list's button): " + filtersInUse().join("; "));
    rememberSelection();
    filter_->clearSearch();
    if (filter_->favoritesOnly()) {
        filter_->setFavoritesOnly(false);
        if (QAction* a = menus_.action("PopupEnableFavorites")) a->setChecked(false);
    }
    filter_->setHaveMiss(GamesFilter::ListAll);
    if (QAction* a = menus_.action("PopupShowHaveMiss")) a->setChecked(true);
    filter_->setParentClone(GamesFilter::Both);
    if (QAction* a = menus_.action("PopupShowParentClone")) a->setChecked(true);
    filter_->setDriver(GamesFilter::AllStatus);
    filter_->setArcadeFilter("allgames");
    if (!filter_->cpuFilter().isEmpty()) {
        filter_->setCpuFilter({}, true);
        ui_->FilterCPU->clear();
        ui_->FilterCPUList->setCurrentIndex(0);
    }
    MiscFilters misc;
    misc.mainCpu = filter_->miscFilters().mainCpu;
    if (misc != filter_->miscFilters()) {
        misc.save(fe_.gamesFiltersFile());
        filter_->setMiscFilters(misc, multiSlot_);
    }
    updateFilterIcons();
    afterFilterChange();
}

void MainWindow::updateEmptyListMessage() {
    QAbstractItemView* view = gamesView();
    if (!view) return;
    QWidget* host = view->viewport();
    if (!emptyList_ || emptyList_->parentWidget() != host) {
        delete emptyList_;
        emptyList_ = new QWidget(host);
        emptyList_->setObjectName("EmptyListMessage");
        auto* lay = new QVBoxLayout(emptyList_);
        lay->setContentsMargins(24, 24, 24, 24);
        lay->setSpacing(10);
        lay->addStretch(2);
        auto* heading = new QLabel;
        heading->setObjectName("EmptyListHeading");
        QFont f = heading->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.35);
        heading->setFont(f);
        heading->setAlignment(Qt::AlignCenter);
        heading->setWordWrap(true);
        auto* text = new QLabel;
        text->setObjectName("EmptyListText");
        text->setAlignment(Qt::AlignCenter);
        text->setWordWrap(true);
        auto* button = new QPushButton;
        button->setObjectName("EmptyListButton");
        button->setCursor(Qt::PointingHandCursor);
        lay->addWidget(heading);
        lay->addWidget(text);
        lay->addSpacing(6);
        lay->addWidget(button, 0, Qt::AlignHCenter);
        lay->addStretch(3);
        host->installEventFilter(new FillParent(emptyList_));
        connect(button, &QPushButton::clicked, this, [this] {
            const QString what = emptyList_->property("action").toString();
            if (what == "setup") openEmulatorSetup();
            else if (what == "create") createGamesList();
            else if (what == "search") ui_->ButtonFilterTitleReset_ToolBar->click();
            else if (what == "filters") clearAllFilters();
        });
    }
    if (filter_->rowCount() > 0 || setupPending_ || setupAsking_ > 0) { // (or a start-up question says it)
        emptyList_->hide();
        return;
    }
    // why it's empty, and what to do
    QString heading, text, button, action;
    const QString name = Frontend::section(sys_);
    if (db_.games().isEmpty()) {
        const bool set = fe_.emulator(SystemId::MAME).isSet() || fe_.emulator(SystemId::HBMAME).isSet();
        if (!set || !QFileInfo::exists(fe_.emulatorFile(sys_))) {
            heading = set ? tr("%1 isn't where EmuLoader expects it.").arg(name) : tr("No emulator is set up yet.");
            text = tr("EmuLoader builds its list of games from %1. Tell it where %1 is, and it can then build the list.").arg(name);
            button = tr("Set Up %1...").arg(set ? name : QStringLiteral("MAME"));
            action = "setup";
        } else {
            heading = tr("There's no games list yet.");
            text = tr("EmuLoader builds its list of games from %1. It takes a minute or two.").arg(name);
            button = tr("Create the Games List");
            action = "create";
        }
    } else if (filter_->searching()) {
        const QString typed = ui_->FilterGameTitle_ToolBar->text();
        heading = typed.isEmpty() ? tr("No games match the search.") : tr("No games match “%1”.").arg(typed);
        const QStringList on = filtersInUse();
        QAction* inc = menus_.action("FilterTextBarIncludeToolBarFilters");
        text = (!on.isEmpty() && (!inc || inc->isChecked()))
                   ? tr("The search looks only among the games the filters show (%1).").arg(on.join(", "))
                   : tr("Check the spelling, or search in another field (the search bar's settings button).");
        button = tr("Clear the Search");
        action = "search";
    } else {
        const QStringList on = filtersInUse();
        heading = tr("No games match the filters in use.");
        text = on.isEmpty() ? tr("The filters leave no games to show.") : on.join(" • ");
        button = tr("Clear the Filters");
        action = "filters";
    }
    emptyList_->findChild<QLabel*>("EmptyListHeading")->setText(heading);
    emptyList_->findChild<QLabel*>("EmptyListText")->setText(text);
    emptyList_->findChild<QPushButton*>("EmptyListButton")->setText(button);
    emptyList_->setProperty("action", action);
    emptyList_->setGeometry(host->rect());
    emptyList_->show();
    emptyList_->raise();
}

} // namespace eld
