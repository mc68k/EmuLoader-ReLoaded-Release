#include "MameIni.h"
#include "TextFile.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace eld {

// ---------------------------------------------------------------- MameIniFile

static QString unquote(const QString& v) {
    return (v.size() >= 2 && v.startsWith('"') && v.endsWith('"')) ? v.mid(1, v.size() - 2) : v;
}

QString MameIniFile::formatLine(const QString& key, const QString& value) {
    QString v = value.contains(' ') && !value.startsWith('"') ? "\"" + value + "\"" : value;
    return QString("%1 %2").arg(key, -25).arg(v).trimmed();
}

void MameIniFile::parse(const QString& text) {
    lines_.clear();
    for (QString raw : text.split('\n')) {
        if (raw.endsWith('\r')) raw.chop(1);
        Line l;
        l.raw = raw;
        QString t = raw.trimmed();
        if (!t.isEmpty() && t[0] != '#' && t[0] != ';' && t[0] != '<') {
            int sp = t.indexOf(QRegularExpression("\\s"));
            l.key = sp < 0 ? t : t.left(sp);
            l.value = sp < 0 ? QString() : unquote(t.mid(sp).trimmed());
        }
        lines_.push_back(l);
    }
    while (!lines_.isEmpty() && lines_.last().raw.isEmpty() && lines_.last().key.isEmpty()) lines_.removeLast();
}

bool MameIniFile::load(const QString& path) {
    QString text;
    if (!readTextFile(path, text)) { lines_.clear(); return false; }
    crlf_ = text.contains("\r\n");
    parse(text);
    return true;
}

QString MameIniFile::toString() const {
    QString out;
    const char* eol = crlf_ ? "\r\n" : "\n";
    for (const Line& l : lines_) out += (l.dirty ? formatLine(l.key, l.value) : l.raw) + eol;
    return out;
}

bool MameIniFile::save(const QString& path) const { return writeTextFile(path, toString(), true); }

int MameIniFile::find(const QString& key) const {
    for (int i = 0; i < lines_.size(); ++i)
        if (!lines_[i].key.isEmpty() && lines_[i].key.compare(key, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString MameIniFile::get(const QString& key, const QString& def) const {
    int i = find(key);
    return i < 0 ? def : lines_[i].value;
}

bool MameIniFile::set(const QString& key, const QString& value) {
    int i = find(key);
    if (i >= 0) {
        if (lines_[i].value == value) return false;
        lines_[i].value = value;
        lines_[i].dirty = true;
        return true;
    }
    Line l;
    l.key = key; l.value = value; l.dirty = true;
    lines_.push_back(l);
    return true;
}

bool MameIniFile::remove(const QString& key) {
    int i = find(key);
    if (i < 0) return false;
    lines_.removeAt(i);
    return true;
}

QStringList MameIniFile::keys() const {
    QStringList k;
    for (const Line& l : lines_) if (!l.key.isEmpty()) k << l.key;
    return k;
}

bool MameIniFile::empty() const {
    for (const Line& l : lines_) if (!l.key.isEmpty()) return false;
    return true;
}

void MameIniFile::addHeader(const QString& comment) {
    if (!lines_.isEmpty()) return;
    for (const QString& s : comment.split('\n')) { Line l; l.raw = "# " + s; lines_.push_back(l); }
    lines_.push_back(Line{});
}

// --------------------------------------------------------------- MameIniChain

QString MameIniChain::levelLabel(int level) {
    static const char* names[kLevels] = {"mame.ini (all machines)", "debug.ini", "vertical.ini", "horizont.ini",
                                         "arcade.ini", "console.ini", "computer.ini", "othersys.ini",
                                         "screen type (raster/vector/lcd/svg).ini", "source (driver).ini",
                                         "BIOS.ini", "parent.ini", "machine.ini"};
    return level >= 0 && level < kLevels ? names[level] : QString();
}

QString MameIniChain::baseName(int level, const ChainMachine& m) {
    bool soft = !m.softwareName.isEmpty();
    switch (level) {
    case 0: return "mame";
    case 1: return "debug";
    case 2: return m.orientation == 1 ? "vertical" : QString();
    case 3: return m.orientation == 0 ? "horizont" : QString();
    case 4: return (!soft || m.systemType == 0) ? "arcade" : QString();
    case 5: return (soft || m.systemType == 1) ? "console" : QString();
    case 6: return (soft || m.systemType == 1) ? "computer" : QString();
    case 7: return (soft || m.systemType == 1) ? "othersys" : QString();
    case 8:
        switch (m.screenType) {
        case 1: return "vector";
        case 2: return "lcd";
        case 3: return "svg";
        default: return "raster";
        }
    case 9: return m.driver.isEmpty() ? QString() : QFileInfo(QString(m.driver).replace(QLatin1Char('\\'), QLatin1Char('/'))).completeBaseName(); // "sony/zn1.cpp" -> zn1
    case 10: return m.bios;
    case 11: return m.cloneOf;
    case 12: return m.name;
    }
    return {};
}

QString MameIniChain::path(int level, const ChainMachine& m, const QString& mameIni, const QString& iniDir,
                           int mameBuild) {
    if (level == 0) return mameIni;
    QString base = baseName(level, m);
    if (base.isEmpty()) return {};
    QDir d(iniDir);
    if (level == 9) {
        // Since MAME 0.179 driver .ini files live only in "<inidir>/source/".
        QString inSource = d.filePath("source/" + base + ".ini");
        if (mameBuild > 118) {
            if (mameBuild < 179 && !QFileInfo::exists(inSource)) return d.filePath(base + ".ini");
            return inSource;
        }
        return d.filePath(base + ".ini");
    }
    return d.filePath(base + ".ini");
}

QMap<QString, QString> MameIniChain::effective(const ChainMachine& m, const QString& mameIni, const QString& iniDir,
                                               int mameBuild, int uptoLevel) {
    QMap<QString, QString> v;
    for (int level = 0; level <= uptoLevel && level < kLevels; ++level) {
        QString p = path(level, m, mameIni, iniDir, mameBuild);
        if (p.isEmpty()) continue;
        MameIniFile f;
        if (!f.load(p)) continue;
        for (const QString& k : f.keys()) v.insert(k.toLower(), f.get(k));
    }
    return v;
}

QStringList splitPathList(const QString& s) {
    QStringList out;
    for (QString p : s.split(';', Qt::SkipEmptyParts)) {
        p = unquote(p.trimmed());
        if (!p.isEmpty()) out << p;
    }
    return out;
}

QString MameIniChain::iniDir(const MameIniFile& mameIni, const QString& mameFolder) {
    QString d = mameIni.get("inipath", "ini");
    if (d.contains("<NULL>") || d.isEmpty() || d == "." || d == ".;ini") d = "ini";
    else {
        d = splitPathList(d).value(0);
        if (d == "." || d.isEmpty()) d = "ini";
    }
    return resolvePath(mameFolder, d);
}

} // namespace eld
