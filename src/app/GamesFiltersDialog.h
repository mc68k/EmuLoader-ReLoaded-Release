// GamesFiltersDialog - Games Filters - MAME and Arcade (the "All Games" tool bar button;
// TFormArcadeGamesFilter, uArcadeGamesFilter.pas): the filters of arcade\filters\mame_filters.ini
// as tiles with their pictures, in groups (General, then Capcom, Sega, ...). Click a group's title
// to fold it; double click or Apply picks a filter; Go to Current shows the one in use. The
// picture size (4K, XL, L, S) is [ListFilter] ArcadeGames_IconSize.
#pragma once

#include <QDialog>
#include <memory>

namespace Ui { class FormArcadeGamesFilter; }
class QListWidget;
class QListWidgetItem;

namespace eld {

class GamesFiltersDialog : public QDialog {
    Q_OBJECT
public:
    // One filter of mame_filters.ini: name ("cps1"), description, group ("" = General).
    struct Filter { QString name, description, group; };
    // false (and a message) when mame_filters.ini is missing or has no [main] section.
    static bool readFilters(const QString& iniFile, QList<Filter>& out, QString* error);

    GamesFiltersDialog(const QList<Filter>& filters, const QString& current, const QString& iconFolder, int iconSize,
                       QWidget* parent = nullptr);
    ~GamesFiltersDialog() override;

    QString selected() const;          // the filter picked ("" = none)
    int iconSize() const { return size_; } // 0 128x128, 1 68x68, 2 48x48, 3 30x24

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void fill();
    void setIconSize(int size);
    void goToCurrent();
    void toggleGroup(QListWidgetItem* header);

    std::unique_ptr<Ui::FormArcadeGamesFilter> ui_;
    QList<Filter> filters_;
    QString current_, iconFolder_;
    int size_ = 2;
    QListWidget* list_ = nullptr;
};

} // namespace eld
