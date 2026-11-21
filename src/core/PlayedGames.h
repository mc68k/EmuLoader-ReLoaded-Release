// PlayedGames - play statistics, shared with the Delphi EmuLoader: arcade\played_games\mame.txt
// (hbmame.txt), one line per game, the last played first (TFormMain.UpdatePlayedGameArcade):
//
//   gamename=times_played;last_played;total_playtime
//   pacman=3;1500000000;754000
//
// last_played is a DOS file date (DateTimeToFileDate: local time, two-second steps) and
// total_playtime is in milliseconds. Software list games are "list\gamename".
#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

namespace eld {

struct PlayStat {
    int times = 0;
    qint32 lastPlayed = 0; // DOS file date, 0 = never
    qint64 playtime = 0;   // milliseconds
};

class PlayedGames {
public:
    bool load(const QString& file);
    PlayStat value(const QString& name) const { return stats_.value(name.toLower()); }
    // The games in the file's order (the last played first), as written.
    const QStringList& order() const { return order_; }
    const QHash<QString, PlayStat>& all() const { return stats_; } // keys in lower case

    // UpdatePlayedGameArcade: one more play of elapsedMs at now; the line moves to the top and the
    // file is written (other lines are kept as they are). Returns the new values.
    PlayStat record(const QString& file, const QString& name, qint64 elapsedMs, const QDateTime& now);

    // ResetPlayGameInfo: what 0 = all (the line goes), 1 = total playtime, 2 = last played,
    // 3 = times played. The lines stay where they are.
    void reset(const QString& file, const QStringList& names, int what);

    static QDateTime fromDosDate(qint32 d);
    static qint32 toDosDate(const QDateTime& t);
    // GetDateTimeStr: "Nov/03/2026 14:05:22" (or without the seconds)
    static QString dateText(qint32 dosDate, bool hideSeconds);
    // GetPlayTime(…, True): "01:02:03", "1 day, 01:02:03" (or without the seconds)
    static QString playtimeText(qint64 ms, bool hideSeconds);

private:
    static bool parseLine(const QString& line, QString* name, PlayStat* s);
    QHash<QString, PlayStat> stats_;
    QStringList order_;
};

} // namespace eld
