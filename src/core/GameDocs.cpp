#include "GameDocs.h"
#include "MameIni.h"
#include "WinIni.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace eld {

// ------------------------------------------------------------------ helpers

QString decodeHtmlEntities(const QString& s) {
    if (!s.contains('&')) return s;
    static const QHash<QString, QString> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", QString(QChar(0xA0))},
    };
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        int end = s.indexOf(';', i + 1);
        if (end < 0 || end - i > 10) {
            out += s[i];
            continue;
        }
        QString ent = s.mid(i + 1, end - i - 1);
        QString rep;
        if (ent.startsWith('#')) {
            bool ok = false;
            uint cp = ent.startsWith("#x", Qt::CaseInsensitive) ? ent.mid(2).toUInt(&ok, 16) : ent.mid(1).toUInt(&ok, 10);
            if (ok) {
                if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = ' ';
                rep = QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
            }
        } else {
            rep = named.value(ent.toLower());
        }
        if (rep.isNull()) {
            out += s[i];
            continue;
        }
        out += rep;
        i = end;
    }
    return out;
}

QByteArray GameDocs::Dat::raw(int i) const {
    int from = starts[i];
    int to = i + 1 < starts.size() ? starts[i + 1] : data.size();
    while (to > from && (data[to - 1] == '\n' || data[to - 1] == '\r')) --to;
    return data.mid(from, to - from);
}

// TFormMain.DecodeUnicodeStr: UTF-8, else the bytes as they are (older files in Latin-1).
QString GameDocs::Dat::line(int i) const {
    QByteArray b = raw(i);
    QString s = QString::fromUtf8(b);
    if (s.contains(QChar::ReplacementCharacter)) s = QString::fromLatin1(b);
    return s;
}

bool GameDocs::read(Dat& d, const QString& file) {
    d = Dat();
    QFile f(file);
    if (file.isEmpty() || !f.open(QIODevice::ReadOnly)) return false;
    d.data = f.readAll();
    if (d.data.startsWith("\xEF\xBB\xBF")) d.data.remove(0, 3);
    if (d.data.isEmpty()) return false;
    d.starts.reserve(d.data.size() / 40);
    d.starts.append(0);
    for (int i = d.data.indexOf('\n'); i >= 0; i = d.data.indexOf('\n', i + 1))
        if (i + 1 < d.data.size()) d.starts.append(i + 1);
    return true;
}

// "$info=pacman" or "$info=pacman,puckman," (history.dat) -> the line, for every name.
void GameDocs::indexInfo(Dat& d) {
    for (int i = 0; i < d.count(); ++i) {
        const char* p = d.data.constData() + d.starts[i];
        if (p[0] != '$' || qstrncmp(p, "$info=", 6) != 0) continue;
        QString names = QString::fromUtf8(d.raw(i).mid(6));
        for (const QString& n : names.split(',', Qt::SkipEmptyParts)) {
            QString key = n.trimmed().toLower();
            if (!key.isEmpty() && !d.index.contains(key)) d.index.insert(key, i);
        }
    }
}

// The first line without its leading "# " (or "##  " for marp.dat).
QString GameDocs::version(const Dat& d, int chop) {
    return d.loaded() ? d.line(0).mid(chop).trimmed() : QString();
}

bool GameDocs::block(const Dat& d, const QString& name, const QStringList& tags, QStringList* out, bool* startSeen) {
    auto it = d.index.constFind(name.toLower());
    if (name.isEmpty() || it == d.index.cend()) return false;
    bool adding = false;
    for (int i = *it + 1; i < d.count(); ++i) {
        QString t = d.line(i).trimmed();
        if (t == "$end") break;
        if (!adding) {
            if (tags.contains(t)) {
                adding = true;
                if (startSeen) *startSeen = true;
            }
            continue;
        }
        out->append(t);
    }
    return true;
}

// Empty lines at the start and end of a text aren't shown.
static void trimLines(QStringList& l) {
    while (!l.isEmpty() && l.first().isEmpty()) l.removeFirst();
    while (!l.isEmpty() && l.last().isEmpty()) l.removeLast();
}

// ------------------------------------------------------------------ loading

QList<QPair<int, bool>> GameDocs::readOrder(const WinIni& ini) {
    QList<QPair<int, bool>> out;
    for (int i = 1; i <= Count; ++i) {
        QString v = ini.readString("GameDocuments", QString("DocIndex%1").arg(i), QString("%1%2").arg(i).arg(1));
        int doc = v.size() == 2 ? v.left(1).toInt() : 0;
        bool dup = std::any_of(out.begin(), out.end(), [doc](const QPair<int, bool>& p) { return p.first == doc; });
        if (doc < 1 || doc > Count || dup) { // GameDocsButtonReset
            out.clear();
            for (int d = 1; d <= Count; ++d) out.append({d, true});
            return out;
        }
        out.append({doc, v.right(1) != "0"});
    }
    return out;
}

QStringList GameDocs::datFolders(const QString& dataDir, const QString& mameFolder) {
    // ToggleGameDocuments: ui.ini next to EmuLoader ("linux mode"), else next to MAME.
    MameIniFile ui;
    if (!ui.load(QDir(dataDir).filePath("ui.ini")) && !ui.load(QDir(mameFolder).filePath("ui.ini")))
        return {QDir::cleanPath(mameFolder)};
    QString v = ui.get("historypath");
    if (v.isEmpty()) v = "dats";
    QStringList out;
    for (QString p : v.split(';', Qt::SkipEmptyParts)) {
        p = p.trimmed();
        if (p.startsWith('"') && p.endsWith('"') && p.size() > 1) p = p.mid(1, p.size() - 2);
        if (p.isEmpty() || p == ".") continue;
        out << QDir::cleanPath(QDir::isAbsolutePath(QDir::fromNativeSeparators(p)) ? QDir::fromNativeSeparators(p)
                                                                                   : QDir(mameFolder).filePath(QDir::fromNativeSeparators(p)));
    }
    out << QDir::cleanPath(mameFolder); // LocateDATFile falls back to MAME's folder
    return out;
}

QString GameDocs::find(const QStringList& folders, const QString& file) const {
    for (const QString& d : folders) {
        QDir dir(d);
        if (dir.exists(file)) return dir.filePath(file);
        // Linux and macOS: the same name in another case (History.xml)
        for (const QString& e : dir.entryList(QDir::Files))
            if (e.compare(file, Qt::CaseInsensitive) == 0) return dir.filePath(e);
    }
    return {};
}

void GameDocs::clear() {
    mameInfo_ = messInfo_ = history_ = story_ = marp_ = gameInit_ = Dat();
    historyXml_ = marpHtml_ = false;
    marpScoreEnd_ = 0;
    files_.clear();
}

bool GameDocs::anyLoaded() const {
    return mameInfo_.loaded() || messInfo_.loaded() || history_.loaded() || story_.loaded() || marp_.loaded() ||
           gameInit_.loaded();
}

int GameDocs::load(const QStringList& folders) {
    clear();
    auto get = [&](Dat& d, const QString& name) {
        QString f = find(folders, name);
        if (!read(d, f)) return false;
        files_ << f;
        return true;
    };
    if (get(mameInfo_, "mameinfo.dat")) indexInfo(mameInfo_);
    if (get(messInfo_, "messinfo.dat")) indexInfo(messInfo_);
    if (get(history_, "history.xml")) {
        historyXml_ = true;
        // '<system name="pacman" />' (newer files add attributes) -> the line, for each name
        static const QByteArray tag = "<system name=\"";
        for (int i = 0; i < history_.count(); ++i) {
            QByteArray l = history_.raw(i);
            int p = l.indexOf(tag);
            if (p < 0) {
                // a software list game: '<item list="nes" name="smb" />' -> "nes\smb" (MameGame::key)
                static const QByteArray item = "<item list=\"", nameTag = "name=\"";
                int q = l.indexOf(item);
                if (q < 0) continue;
                int ls = q + item.size(), le = l.indexOf('"', ls);
                int n = l.indexOf(nameTag, le), ns = n + nameTag.size(), ne = n < 0 ? -1 : l.indexOf('"', ns);
                if (le <= ls || ne <= ns) continue;
                QString key = QString::fromUtf8(l.mid(ls, le - ls) + '\\' + l.mid(ns, ne - ns)).toLower();
                if (!history_.index.contains(key)) history_.index.insert(key, i);
                continue;
            }
            int s = p + tag.size(), e = l.indexOf('"', s);
            if (e <= s) continue;
            QString key = QString::fromUtf8(l.mid(s, e - s)).toLower();
            if (!history_.index.contains(key)) history_.index.insert(key, i);
        }
    } else if (get(history_, "history.dat")) {
        indexInfo(history_);
    }
    if (get(story_, "story.dat")) indexInfo(story_);
    if (get(marp_, "scores3.htm")) {
        marpHtml_ = true;
        for (int i = 2; i < marp_.count(); ++i) {
            QString l = marp_.line(i);
            if (!marpScoreEnd_) {
                int p = l.indexOf(": - score -");
                if (p >= 0) marpScoreEnd_ = p + 13; // where the score column ends (MarpData's iPos2H)
            }
            QString t = l.trimmed();
            int sp = t.indexOf(' ');
            if (sp > 0) {
                QString key = t.left(sp).toLower();
                if (!marp_.index.contains(key)) marp_.index.insert(key, i);
            }
        }
    } else if (get(marp_, "marp.dat")) {
        indexInfo(marp_);
    }
    if (get(gameInit_, "gameinit.dat")) indexInfo(gameInit_);
    return files_.size();
}

// ------------------------------------------------------------------ documents

// InformationData: "$info=<set>" (or its parent) in mameinfo.dat, else messinfo.dat; the text after "$mame".
bool GameDocs::info(const QString& name, const QString& cloneOf, Section& s) const {
    for (const Dat* d : {&mameInfo_, &messInfo_}) {
        if (!d->loaded()) continue;
        QStringList lines;
        bool seen = false;
        if (!block(*d, name, {"$mame"}, &lines, &seen) && !block(*d, cloneOf, {"$mame"}, &lines, &seen)) continue;
        if (!seen) return false; // found, but no game text in it
        s.version = version(*d, 2);
        s.lines = lines;
        return true;
    }
    return false;
}

// GameDriverData: "$info=<source file>" in mameinfo.dat / messinfo.dat, the text after "$drv".
bool GameDocs::driver(const QString& driverFile, Section& s) const {
    if (driverFile.isEmpty()) return false;
    // Newer MAME source names have a folder ("namco/pacman.cpp"); the DAT may list either.
    QStringList names{driverFile, QFileInfo(driverFile).fileName()};
    for (const Dat* d : {&mameInfo_, &messInfo_}) {
        if (!d->loaded()) continue;
        for (const QString& n : names) {
            QStringList lines;
            if (!block(*d, n, {"$drv"}, &lines)) continue;
            s.version = version(*d, 2);
            s.lines = lines;
            return true;
        }
    }
    return false;
}

// HistoryXML / HistoryData: history.xml's <entry> with <system name="<set>" />, or history.dat's
// "$info=...,<set>," block after "$bio". Links (<a href="...">title</a>) become "title address".
bool GameDocs::history(const QString& name, Section& s) const {
    if (!history_.loaded()) return false;
    auto it = history_.index.constFind(name.toLower());
    if (it == history_.index.cend()) return false;
    auto link = [](QString l) {
        int a = l.indexOf("<a href=");
        if (a < 0) return l;
        int q1 = l.indexOf('"', a), q2 = q1 >= 0 ? l.indexOf('"', q1 + 1) : -1, gt = l.indexOf('>', a);
        if (q1 < 0 || q2 < 0 || gt < 0) return l;
        QString href = l.mid(q1 + 1, q2 - q1 - 1);
        if (!href.startsWith("http", Qt::CaseInsensitive)) href = "https://www.arcade-history.com/" + href;
        QString title = l.left(a) + l.mid(gt + 1);
        title.remove("</a>");
        return (title.trimmed() + " " + href).trimmed();
    };
    QStringList lines;
    if (historyXml_) {
        int i = *it;
        // find "<text>" in this entry (the text may start on the same line)
        QString first;
        bool found = false;
        for (; i < history_.count(); ++i) {
            QString t = history_.line(i).trimmed();
            if (t.contains("</entry>")) break;
            int p = t.indexOf("<text>");
            if (p >= 0) {
                first = t.mid(p + 6);
                found = true;
                break;
            }
        }
        if (!found) return false;
        QString t = first;
        for (bool firstLine = true;; firstLine = false) {
            if (!firstLine) {
                if (++i >= history_.count()) break;
                t = history_.line(i).trimmed();
            }
            int end = t.indexOf("</text>");
            if (end >= 0) t = t.left(end).trimmed();
            lines << link(decodeHtmlEntities(t));
            if (end >= 0) break;
        }
    } else {
        bool adding = false;
        for (int i = *it + 1; i < history_.count(); ++i) {
            QString t = history_.line(i).trimmed();
            if (t == "$end") break;
            if (!adding) {
                adding = t == "$bio";
                continue;
            }
            lines << link(t);
        }
    }
    trimLines(lines);
    if (lines.isEmpty()) return false;
    s.lines = lines;
    return true;
}

// StoryData: "$info=<set>" in story.dat, the text after "$bio" or "$story".
bool GameDocs::story(const QString& name, Section& s) const {
    if (!story_.loaded() || !block(story_, name, {"$bio", "$story"}, &s.lines)) return false;
    s.version = version(story_, 2);
    return true;
}

// MarpData: the first three scores in scores3.htm, or "$info=<set>" in marp.dat after "$marp".
bool GameDocs::marp(const QString& name, Section& s) const {
    if (!marp_.loaded()) return false;
    if (!marpHtml_) {
        if (!block(marp_, name, {"$marp"}, &s.lines)) return false;
        s.version = version(marp_, 4);
        return true;
    }
    auto it = marp_.index.constFind(name.toLower());
    if (it == marp_.index.cend()) return false;
    int at = *it;
    s.version = marp_.line(1).trimmed();
    s.lines << QString("[ http://replay.marpirc.net/r/%1 ]").arg(name) << QString();
    QString l0 = marp_.line(at);
    int col = l0.indexOf(':') + 1; // where the score starts
    for (int k = 0; k < 3 && at + k < marp_.count(); ++k) {
        QString score = marp_.line(at + k).mid(col);
        int cut = marpScoreEnd_ ? score.indexOf(':', marpScoreEnd_) : -1;
        if (cut >= 0) score = score.left(cut);
        s.lines << QString(" %1.    %2").arg(k + 1).arg(score.trimmed());
    }
    return true;
}

// GameInitData: "$info=<set>" (or its parent) in gameinit.dat, the text after "$mame".
bool GameDocs::gameInit(const QString& name, const QString& cloneOf, Section& s) const {
    if (!gameInit_.loaded()) return false;
    bool seen = false;
    if (!block(gameInit_, name, {"$mame"}, &s.lines, &seen) && !block(gameInit_, cloneOf, {"$mame"}, &s.lines, &seen))
        return false;
    return seen;
}

QList<GameDocs::Section> GameDocs::sections(const QString& name, const QString& cloneOf, const QString& driverFile,
                                             const QList<int>& order) const {
    QList<Section> out;
    for (int doc : order) {
        Section s;
        s.doc = doc;
        bool ok = false;
        switch (doc) {
        case Info: ok = info(name, cloneOf, s); break;
        case History: ok = history(name, s); break;
        case Driver: ok = driver(driverFile, s); break;
        case Story: ok = story(name, s); break;
        case Marp: ok = marp(name, s); break;
        case GameInit: ok = gameInit(name, cloneOf, s); break;
        default: break;
        }
        if (!ok) continue;
        trimLines(s.lines);
        out.append(s);
    }
    return out;
}

} // namespace eld
