#include "PlayedGames.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>

namespace eld {

// GetPlayedGameInfoIni: "times;last;total", older lines "times;last" or "times"; anything from
// "<title>" on is ignored.
bool PlayedGames::parseLine(const QString& line, QString* name, PlayStat* s) {
    int eq = line.indexOf('=');
    if (eq <= 0) return false;
    *name = line.left(eq).trimmed();
    QString v = line.mid(eq + 1);
    int t = v.indexOf("<title>");
    if (t >= 0) v = v.left(t);
    QStringList parts = v.split(';');
    *s = PlayStat();
    s->times = parts.value(0).trimmed().toInt();
    s->lastPlayed = parts.value(1).trimmed().toInt();
    s->playtime = parts.value(2).trimmed().toLongLong();
    return !name->isEmpty();
}

bool PlayedGames::load(const QString& file) {
    stats_.clear();
    order_.clear();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    for (const QByteArray& raw : f.readAll().split('\n')) {
        QString line = QString::fromUtf8(raw).trimmed();
        QString name;
        PlayStat s;
        if (!parseLine(line, &name, &s)) continue;
        QString key = name.toLower();
        if (stats_.contains(key)) continue; // IndexOfName finds the first
        stats_.insert(key, s);
        order_ << name;
    }
    return true;
}

PlayStat PlayedGames::record(const QString& file, const QString& name, qint64 elapsedMs, const QDateTime& now) {
    QStringList lines;
    QFile in(file);
    if (in.open(QIODevice::ReadOnly)) {
        for (QByteArray raw : in.readAll().split('\n')) {
            if (raw.endsWith('\r')) raw.chop(1);
            lines << QString::fromUtf8(raw);
        }
        in.close();
        while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    }
    PlayStat s;
    for (int i = 0; i < lines.size(); ++i) { // PlayedList.IndexOfName
        QString n;
        PlayStat old;
        if (parseLine(lines[i], &n, &old) && n.compare(name, Qt::CaseInsensitive) == 0) {
            s = old;
            lines.removeAt(i);
            break;
        }
    }
    s.times += 1;
    s.lastPlayed = toDosDate(now);
    s.playtime += std::max<qint64>(0, elapsedMs);
    lines.prepend(QString("%1=%2;%3;%4").arg(name).arg(s.times).arg(s.lastPlayed).arg(s.playtime));
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write((lines.join("\r\n") + "\r\n").toUtf8());
        out.commit();
    }
    load(file);
    return s;
}

void PlayedGames::reset(const QString& file, const QStringList& names, int what) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return;
    QStringList lines;
    for (QByteArray raw : in.readAll().split('\n')) {
        if (raw.endsWith('\r')) raw.chop(1);
        lines << QString::fromUtf8(raw);
    }
    in.close();
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    bool changed = false;
    for (const QString& want : names)
        for (int i = 0; i < lines.size(); ++i) {
            QString n;
            PlayStat st;
            if (!parseLine(lines[i], &n, &st) || n.compare(want, Qt::CaseInsensitive) != 0) continue;
            if (what == 0) {
                lines.removeAt(i);
            } else {
                if (what == 1) st.playtime = 0;
                if (what == 2) st.lastPlayed = 0;
                if (what == 3) st.times = 0;
                lines[i] = QString("%1=%2;%3;%4").arg(n).arg(st.times).arg(st.lastPlayed).arg(st.playtime);
            }
            changed = true;
            break;
        }
    if (changed) {
        QSaveFile out(file);
        if (out.open(QIODevice::WriteOnly)) {
            out.write((lines.join("\r\n") + (lines.isEmpty() ? "" : "\r\n")).toUtf8());
            out.commit();
        }
    }
    load(file);
}

// DOS date/time: bits 0-4 seconds/2, 5-10 minutes, 11-15 hours, 16-20 day, 21-24 month, 25-31 year-1980.
QDateTime PlayedGames::fromDosDate(qint32 d) {
    if (d == 0 || d == -1) return {};
    quint32 v = quint32(d);
    QDate date(int((v >> 25) & 0x7F) + 1980, int((v >> 21) & 0x0F), int((v >> 16) & 0x1F));
    QTime time(int((v >> 11) & 0x1F), int((v >> 5) & 0x3F), int((v & 0x1F) * 2));
    if (!date.isValid() || !time.isValid()) return {};
    return QDateTime(date, time, Qt::LocalTime);
}

qint32 PlayedGames::toDosDate(const QDateTime& t) {
    QDateTime l = t.toLocalTime();
    QDate d = l.date();
    QTime tm = l.time();
    if (d.year() < 1980) return 0;
    quint32 v = (quint32(d.year() - 1980) << 25) | (quint32(d.month()) << 21) | (quint32(d.day()) << 16) |
                (quint32(tm.hour()) << 11) | (quint32(tm.minute()) << 5) | quint32(tm.second() / 2);
    return qint32(v);
}

QString PlayedGames::dateText(qint32 dosDate, bool hideSeconds) {
    QDateTime t = fromDosDate(dosDate);
    if (!t.isValid()) return {};
    return QLocale().toString(t, hideSeconds ? "MMM/dd/yyyy HH:mm" : "MMM/dd/yyyy HH:mm:ss");
}

QString PlayedGames::playtimeText(qint64 ms, bool hideSeconds) {
    if (ms <= 0) return {};
    qint64 sec = ms / 1000;
    qint64 days = sec / 86400, hours = (sec % 86400) / 3600, minutes = (sec % 3600) / 60, seconds = sec % 60;
    QString t = hideSeconds ? QString::asprintf("%02lld:%02lld", hours, minutes)
                            : QString::asprintf("%02lld:%02lld:%02lld", hours, minutes, seconds);
    if (days > 0)
        t = (days == 1 ? QCoreApplication::translate("eld::PlayedGames", "1 day, %1")
                       : QCoreApplication::translate("eld::PlayedGames", "%1 days, %2").arg(days))
                .arg(t);
    return t;
}

} // namespace eld
