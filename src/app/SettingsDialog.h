// SettingsDialog - Menu Options > Settings: every EmuLoader setting in one window, a tab per
// subject (General, Games List, Game Info and Documents, Colors, Night Mode, Pictures, Layouts,
// Thumbnails, Video, Emulators, Tool Bar, Splash Screen), in place of the original's separate
// windows (Preferences and its two pages, Video Preview Settings, Category Settings, Layout
// Settings, Thumbnail Settings, Emulators Setup, Customize Night Mode, Customize Tool Bar,
// Customize Splash Screen). The menu items that opened those open this window at their tab.
//
// A tab holds a part of one of those windows, as it is (its own code, its own ini keys): OK
// saves every tab (as each window's OK did), Cancel leaves them all as they were.
#pragma once

#include <QDialog>
#include <QVector>
#include <functional>

class QTabWidget;

namespace eld {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // A tab: w (shown in a scroll area when it's bigger than the window). ok saves it (false:
    // it refused, e.g. an invalid entry: the window stays open at that tab); cancel undoes
    // what it showed meanwhile.
    int addPage(const QString& title, QWidget* w, std::function<bool()> ok = {}, std::function<void()> cancel = {});
    // A tab built when it's first shown (or by buildAll): build() adds its page with addPage or
    // addDialog, which then fill this tab. Until then it has no OK or Cancel to run: a tab that
    // wasn't built wasn't changed.
    int addLazyPage(const QString& title, std::function<void()> build);
    void ensureBuilt(int index);
    void buildAll();
    // After the window shows: the tabs not built yet, one at a time while it's idle
    void buildRestWhenIdle();
    // As wide as all the tabs' names and the find box (the tabs don't scroll), as the screen allows
    void widenForTabs();
    // A window's content as a tab: its own OK/Cancel buttons (and their panel) hidden, OK and
    // Cancel done by clicking them, so it saves as it did.
    static void embed(QDialog* d, const QStringList& hide);
    // A window as a tab: embedded, its OK button clicked for OK (it must end accepted), its
    // Cancel button (or reject) for Cancel. okButton "" = its accept(); hide: more to hide.
    int addDialog(const QString& title, QDialog* d, const QString& okButton, const QString& cancelButton,
                  const QStringList& hide = {});

    // The tab's content sized to the room it has (called as the window is resized), in place of
    // scroll bars when there's more room than it needs.
    void setPageFit(int index, std::function<void(const QSize&)> fit);

    QTabWidget* tabs() const { return tabs_; }
    void setCurrentPage(int index);
    // The tabs in another order: order lists the tabs' indexes as they are, in the new order
    void arrange(const QList<int>& order);
    void showPageOf(QWidget* inside); // the tab that holds it
    int pageIndex(const QString& title) const;

    void accept() override;
    void reject() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    struct Page {
        QWidget* widget = nullptr;
        std::function<bool()> ok;
        std::function<void()> cancel;
        std::function<void(const QSize&)> fit;
        std::function<void()> build; // (a tab not built yet)
    };
    void growFor(QWidget* w);
    bool eventFilter(QObject* o, QEvent* e) override;
    QVector<Page> pages_;
    QTabWidget* tabs_ = nullptr;
    bool done_ = false;
    bool busy_ = false; // running the tabs' OK or Cancel
    int filling_ = -1;  // the tab being built: addPage fills it
};

} // namespace eld
