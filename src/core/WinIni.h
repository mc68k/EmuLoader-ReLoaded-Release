// WinIni - Windows-style .ini ([Section] / Key=Value), like Delphi's TMemIniFile.
// Section and key names are case-insensitive. Saving keeps comments, order and
// untouched lines exactly as they were, so EmuLoader.ini stays readable by the
// original Delphi EmuLoader.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class WinIni {
public:
    bool load(const QString& path);
    bool save(const QString& path = {}) const;
    const QString& path() const { return path_; }

    QString readString(const QString& section, const QString& key, const QString& def = {}) const;
    int readInteger(const QString& section, const QString& key, int def = 0) const;
    bool readBool(const QString& section, const QString& key, bool def = false) const;
    void writeString(const QString& section, const QString& key, const QString& value);
    void writeInteger(const QString& section, const QString& key, int v) { writeString(section, key, QString::number(v)); }
    void writeBool(const QString& section, const QString& key, bool v) { writeString(section, key, v ? "1" : "0"); }

    bool sectionExists(const QString& section) const;
    QStringList sections() const;
    QStringList keys(const QString& section) const;
    void eraseSection(const QString& section);
    void deleteKey(const QString& section, const QString& key);

private:
    struct Line { QString raw, section, key, value; bool isKey = false; };
    int find(const QString& section, const QString& key) const;
    QVector<Line> lines_;
    QString path_;
    bool utf8_ = false; // ANSI (Windows-1252) unless the file was UTF-8, like TMemIniFile
};

} // namespace eld
