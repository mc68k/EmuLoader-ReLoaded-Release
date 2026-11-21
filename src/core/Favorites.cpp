#include "Favorites.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace eld {

bool Favorites::load(const QString& file, const QString& system) {
    file_ = file;
    system_ = system.toLower();
    lines_.clear();
    names_.clear();
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // TStringList.LoadFromFile: ANSI (or UTF-8 with a BOM); game names are plain ASCII.
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    QString text = QString::fromUtf8(data);
    text.replace("\r\n", "\n");
    lines_ = text.split('\n');
    if (!lines_.isEmpty() && lines_.last().isEmpty()) lines_.removeLast(); // the final line break
    for (const QString& l : lines_) {
        int eq = l.indexOf('=');
        if (eq <= 0 || l.mid(eq + 1).compare(system_, Qt::CaseInsensitive) != 0) continue;
        QString name = l.left(eq);
        if (!name.contains('\\')) names_.insert(name); // software list entries aren't listed here
    }
    return true;
}

bool Favorites::save() const {
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QSaveFile f(file_);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QByteArray out;
    for (const QString& l : lines_) out += l.toUtf8() + "\r\n"; // TStringList.SaveToFile
    f.write(out);
    return f.commit();
}

bool Favorites::add(const QString& name) {
    if (names_.contains(name)) return false;
    names_.insert(name);
    lines_ << entry(name);
    return true;
}

bool Favorites::remove(const QString& name) {
    if (!names_.remove(name)) return false;
    for (int i = 0; i < lines_.size(); ++i) {
        int eq = lines_[i].indexOf('=');
        if (eq > 0 && lines_[i].left(eq) == name && lines_[i].mid(eq + 1).compare(system_, Qt::CaseInsensitive) == 0) {
            lines_.removeAt(i);
            break;
        }
    }
    return true;
}

// ------------------------------------------------------------ profiles

namespace FavoriteProfiles {

static QStringList readLines(const QString& path, bool* ok = nullptr) {
    QFile f(path);
    if (ok) *ok = f.open(QIODevice::ReadOnly);
    else f.open(QIODevice::ReadOnly);
    if (!f.isOpen()) return {};
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    QString text = QString::fromUtf8(data);
    text.replace("\r\n", "\n");
    QStringList lines = text.split('\n');
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines;
}

static bool writeLines(const QString& path, const QStringList& lines) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QByteArray out;
    for (const QString& l : lines) out += l.toUtf8() + "\r\n";
    f.write(out);
    return f.commit();
}

static int titleLine(const QStringList& lines) {
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].startsWith("favorite_title=", Qt::CaseInsensitive)) return i;
    return -1;
}

static bool isDefaultFile(const QString& file) { return file.compare("favorites.txt", Qt::CaseInsensitive) == 0; }

Profile read(const QString& folder, const QString& file) {
    Profile p;
    p.file = file;
    QString path = QDir(folder).filePath(file);
    bool ok = false;
    QStringList lines = readLines(path, &ok);
    int t = titleLine(lines);
    if (isDefaultFile(file)) {
        p.title = QStringLiteral("Default");
    } else {
        if (t >= 0) p.title = lines[t].mid(lines[t].indexOf('=') + 1);
        if (p.title.isEmpty()) p.title = QFileInfo(file).completeBaseName();
        if (ok && t < 0) { // "favorites profile have no title so, give it one"
            lines.prepend("favorite_title=" + p.title);
            writeLines(path, lines);
            t = 0;
        }
    }
    if (ok) {
        p.games = int(lines.size()) - (t >= 0 ? 1 : 0);
        p.modified = QFileInfo(path).lastModified();
    }
    return p;
}

QList<Profile> list(const QString& folder) {
    QDir d(folder);
    QStringList files = d.entryList({"*.txt"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    if (files.isEmpty()) {
        if (writeLines(d.filePath("favorites.txt"), {})) files << "favorites.txt";
    }
    QList<Profile> out;
    for (const QString& f : files) out << read(folder, f);
    return out;
}

QString title(const QString& folder, const QString& file) { return read(folder, file).title; }

QString create(const QString& folder, const QString& copyOf) {
    QDir d(folder);
    d.mkpath(".");
    QString base = copyOf.isEmpty() ? QStringLiteral("favorites") : QFileInfo(copyOf).completeBaseName();
    if (copyOf.isEmpty() && !d.exists("favorites.txt")) {
        if (!writeLines(d.filePath("favorites.txt"), {})) return {};
        return QStringLiteral("favorites.txt");
    }
    for (int i = 1; i <= 1000; ++i) {
        QString name = base + QString::number(i) + ".txt";
        if (d.exists(name)) continue;
        QStringList lines;
        if (!copyOf.isEmpty()) {
            lines = readLines(d.filePath(copyOf));
            int t = titleLine(lines);
            if (t >= 0) lines.removeAt(t);
        }
        lines.prepend("favorite_title=" + base + QString::number(i));
        return writeLines(d.filePath(name), lines) ? name : QString();
    }
    return {};
}

bool setTitle(const QString& folder, const QString& file, const QString& title) {
    if (isDefaultFile(file) || title.trimmed().isEmpty()) return false; // "Default" is fixed
    QString path = QDir(folder).filePath(file);
    bool ok = false;
    QStringList lines = readLines(path, &ok);
    if (!ok) return false;
    int t = titleLine(lines);
    if (t >= 0) lines[t] = "favorite_title=" + title.trimmed();
    else lines.prepend("favorite_title=" + title.trimmed());
    return writeLines(path, lines);
}

bool rename(const QString& folder, const QString& from, const QString& to) {
    if (isDefaultFile(from) || to.isEmpty()) return false;
    QDir d(folder);
    if (from.compare(to, Qt::CaseInsensitive) != 0 && d.exists(to)) return false;
    return d.rename(from, to);
}

bool clear(const QString& folder, const QString& file) {
    QString path = QDir(folder).filePath(file);
    bool ok = false;
    QStringList lines = readLines(path, &ok);
    if (!ok) return false;
    int t = titleLine(lines);
    QStringList kept;
    if (t >= 0 && !isDefaultFile(file)) kept << lines[t];
    return writeLines(path, kept);
}

bool remove(const QString& folder, const QString& file) {
    if (isDefaultFile(file)) return false; // there must always be the default profile
    return QFile::remove(QDir(folder).filePath(file));
}

QStringList entries(const QString& folder, const QString& file) {
    QStringList lines = readLines(QDir(folder).filePath(file));
    int t = titleLine(lines);
    if (t >= 0) lines.removeAt(t);
    lines.removeAll(QString());
    return lines;
}

int removeEntries(const QString& folder, const QString& file, const QStringList& gone) {
    QString path = QDir(folder).filePath(file);
    bool ok = false;
    QStringList lines = readLines(path, &ok);
    if (!ok) return 0;
    QSet<QString> drop(gone.begin(), gone.end());
    int before = int(lines.size());
    QStringList kept;
    for (const QString& l : lines)
        if (!drop.contains(l) || l.startsWith("favorite_title=", Qt::CaseInsensitive)) kept << l;
    int n = before - int(kept.size());
    if (n > 0 && !writeLines(path, kept)) return 0;
    return n;
}

bool addEntries(const QString& folder, const QString& file, const QStringList& add) {
    QString path = QDir(folder).filePath(file);
    bool ok = false;
    QStringList lines = readLines(path, &ok);
    if (!ok) return false;
    QSet<QString> have(lines.begin(), lines.end());
    for (const QString& l : add)
        if (!l.isEmpty() && !have.contains(l)) {
            lines << l;
            have.insert(l);
        }
    return writeLines(path, lines);
}

} // namespace FavoriteProfiles

} // namespace eld
