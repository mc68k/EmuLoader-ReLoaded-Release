#include "WinIni.h"
#include "TextFile.h"

#include <QFile>
#include <QSaveFile>

namespace eld {

bool WinIni::load(const QString& path) {
    path_ = path;
    lines_.clear();
    QString text;
    utf8_ = false;
    if (!readTextFile(path, text, &utf8_)) return false;
    QString section;
    for (const QString& raw : text.split('\n')) {
        Line l;
        l.raw = raw.endsWith('\r') ? raw.chopped(1) : raw;
        QString t = l.raw.trimmed();
        if (t.startsWith('[') && t.endsWith(']')) section = t.mid(1, t.size() - 2);
        l.section = section; // blank lines and comments belong to the section they are in
        if (t.startsWith('[') && t.endsWith(']')) {
        } else if (!t.isEmpty() && !t.startsWith(';') && !t.startsWith('#')) {
            int eq = t.indexOf('=');
            if (eq > 0) {
                l.isKey = true;
                l.section = section;
                l.key = t.left(eq).trimmed();
                l.value = t.mid(eq + 1); // TMemIniFile keeps value whitespace after '='
            }
        }
        lines_.push_back(l);
    }
    if (!lines_.isEmpty() && lines_.last().raw.isEmpty()) lines_.removeLast();
    return true;
}

bool WinIni::save(const QString& path) const {
    QString out;
    for (const Line& l : lines_) {
        out += l.isKey ? l.key + "=" + l.value : l.raw;
        out += "\r\n";
    }
    return writeTextFile(path.isEmpty() ? path_ : path, out, utf8_);
}

int WinIni::find(const QString& section, const QString& key) const {
    for (int i = 0; i < lines_.size(); ++i)
        if (lines_[i].isKey && lines_[i].key.compare(key, Qt::CaseInsensitive) == 0 &&
            lines_[i].section.compare(section, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QString WinIni::readString(const QString& section, const QString& key, const QString& def) const {
    int i = find(section, key);
    return i < 0 ? def : lines_[i].value;
}

int WinIni::readInteger(const QString& section, const QString& key, int def) const {
    bool ok = false;
    QString s = readString(section, key).trimmed();
    int v = s.startsWith('$') ? s.mid(1).toInt(&ok, 16) : s.toInt(&ok);
    return ok ? v : def;
}

bool WinIni::readBool(const QString& section, const QString& key, bool def) const {
    return readInteger(section, key, def ? 1 : 0) != 0;
}

void WinIni::writeString(const QString& section, const QString& key, const QString& value) {
    int i = find(section, key);
    if (i >= 0) { lines_[i].value = value; return; }
    // Append at the end of the section (or create it).
    int last = -1;
    for (int k = 0; k < lines_.size(); ++k)
        if (lines_[k].section.compare(section, Qt::CaseInsensitive) == 0) last = k;
    Line l;
    l.isKey = true; l.section = section; l.key = key; l.value = value;
    if (last < 0) {
        // As TMemIniFile writes it: "[section]", the keys, then an empty line.
        if (!lines_.isEmpty() && (lines_.last().isKey || !lines_.last().raw.trimmed().isEmpty())) {
            Line blank; blank.section = lines_.last().section; lines_.push_back(blank);
        }
        Line hdr; hdr.raw = "[" + section + "]"; hdr.section = section;
        lines_.push_back(hdr);
        lines_.push_back(l);
        Line blank; blank.section = section;
        lines_.push_back(blank);
    } else {
        // insert after the last key/header of the section, before trailing blank lines
        int pos = last;
        while (pos > 0 && !lines_[pos].isKey && lines_[pos].raw.trimmed().isEmpty()) --pos;
        lines_.insert(pos + 1, l);
    }
}

bool WinIni::sectionExists(const QString& section) const {
    for (const Line& l : lines_)
        if (!l.isKey && l.raw.trimmed().startsWith('[') && l.section.compare(section, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QStringList WinIni::sections() const {
    QStringList s;
    for (const Line& l : lines_)
        if (!l.isKey && l.raw.trimmed().startsWith('[')) s << l.section;
    return s;
}

QStringList WinIni::keys(const QString& section) const {
    QStringList k;
    for (const Line& l : lines_)
        if (l.isKey && l.section.compare(section, Qt::CaseInsensitive) == 0) k << l.key;
    return k;
}

void WinIni::eraseSection(const QString& section) {
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(),
                                [&](const Line& l) { return l.section.compare(section, Qt::CaseInsensitive) == 0; }),
                 lines_.end());
}

void WinIni::deleteKey(const QString& section, const QString& key) {
    int i = find(section, key);
    if (i >= 0) lines_.removeAt(i);
}

} // namespace eld
