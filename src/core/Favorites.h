// Favorites - the original's favorites profiles: <data>\favorites\<[Preferences] FavoriteFile>,
// favorites.txt by default. One "name=system" line per game (e.g. "pacman=mame",
// "sf2=hbmame"); software list entries are "list\name=mame". Other profiles may start with a
// "favorite_title=..." line. Lines this port doesn't use (other systems, custom games) are kept.
#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace eld {

class Favorites {
public:
    // system: "mame" or "hbmame" (GetArcadeSystemIniSection(..., True))
    bool load(const QString& file, const QString& system);
    bool save() const;
    const QSet<QString>& names() const { return names_; }
    bool contains(const QString& name) const { return names_.contains(name); }
    bool add(const QString& name);    // false if already there
    bool remove(const QString& name); // false if not there
    const QString& file() const { return file_; }

private:
    QString entry(const QString& name) const { return name + '=' + system_; }
    QString file_, system_;
    QStringList lines_;
    QSet<QString> names_;
};

// The favorites profiles in <data>\favorites\ (TFormFavoritesManager): every .txt file is a
// profile. favorites.txt is the default one, titled "Default" and never deleted; the others start
// with "favorite_title=...". The games count is the number of other lines.
namespace FavoriteProfiles {

struct Profile {
    QString file;       // "favorites1.txt"
    QString title;      // favorite_title, "Default" for favorites.txt
    int games = 0;
    QDateTime modified; // invalid when the file is missing
    bool isDefault() const { return file.compare("favorites.txt", Qt::CaseInsensitive) == 0; }
};

// GetFavFilesList + ELV_AddItem: the profiles, favorites.txt created when there's none. A profile
// without a title gets one (its file name), written to the file as the original does.
QList<Profile> list(const QString& folder);
Profile read(const QString& folder, const QString& file);
QString title(const QString& folder, const QString& file); // GetFavoritesTitle
// CreateNewValidFileName: a new empty profile (favorites1.txt ... favorites1000.txt), or a copy
// of `copyOf` (its name plus a number). Returns the new file name, empty when none is free.
QString create(const QString& folder, const QString& copyOf = {});
bool setTitle(const QString& folder, const QString& file, const QString& title);
bool rename(const QString& folder, const QString& from, const QString& to);
bool clear(const QString& folder, const QString& file); // all games out, the title kept
bool remove(const QString& folder, const QString& file);
// The game lines of a profile (no title), as written: "pacman=mame", "nes\\smb=mame", "001 2 <file>...".
QStringList entries(const QString& folder, const QString& file);
int removeEntries(const QString& folder, const QString& file, const QStringList& lines); // how many went
bool addEntries(const QString& folder, const QString& file, const QStringList& lines); // at the end, none twice

} // namespace FavoriteProfiles

} // namespace eld
