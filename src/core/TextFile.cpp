#include "TextFile.h"

#include <QCoreApplication>
#include <QDir>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <algorithm>

namespace eld {

static QString decodeCp1252(const QByteArray& b) {
    static const char16_t hi[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                    0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    QString s;
    s.reserve(b.size());
    for (unsigned char c : b) s.append(QChar(c >= 0x80 && c < 0xA0 ? hi[c - 0x80] : char16_t(c)));
    return s;
}

static QByteArray encodeCp1252(const QString& s) {
    QByteArray b;
    b.reserve(s.size());
    for (QChar ch : s) {
        char16_t u = ch.unicode();
        if (u < 0x80 || (u >= 0xA0 && u < 0x100)) { b.append(char(u)); continue; }
        static const char16_t hi[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                        0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
        int k = 0;
        while (k < 32 && hi[k] != u) ++k;
        b.append(k < 32 ? char(0x80 + k) : '?');
    }
    return b;
}

bool readTextFile(const QString& path, QString& out, bool* wasUtf8) {
    if (path.isEmpty()) return false; // e.g. the mame.ini of an emulator that isn't set up
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) return false;
    QByteArray b = f.readAll();
    if (b.startsWith("\xEF\xBB\xBF")) {
        out = QString::fromUtf8(b.mid(3));
        if (wasUtf8) *wasUtf8 = true;
        return true;
    }
    if (b.startsWith("\xFF\xFE")) { // UTF-16 LE (Delphi TEncoding.Unicode)
        out = QString::fromUtf16(reinterpret_cast<const char16_t*>(b.constData() + 2), (b.size() - 2) / 2);
        if (wasUtf8) *wasUtf8 = true;
        return true;
    }
    QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString s = dec.decode(b);
    if (!dec.hasError()) {
        out = s;
        // Plain ASCII counts as ANSI: that's what Delphi's TMemIniFile/TStringList write
        // when no encoding is given, and what the original EmuLoader expects to read.
        bool nonAscii = std::any_of(b.cbegin(), b.cend(), [](char c) { return (unsigned char)c >= 0x80; });
        if (wasUtf8) *wasUtf8 = nonAscii;
    } else {
        out = decodeCp1252(b);
        if (wasUtf8) *wasUtf8 = false;
    }
    return true;
}

bool writeTextFile(const QString& path, const QString& text, bool utf8, bool bom) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QFile::WriteOnly)) return false;
    if (utf8 && bom) f.write("\xEF\xBB\xBF");
    f.write(utf8 ? text.toUtf8() : encodeCp1252(text));
    return f.commit();
}

QString toNative(const QString& p) { return QDir::fromNativeSeparators(QString(p).replace('\\', '/')); }

QString resolvePath(const QString& base, const QString& p) {
    if (p.isEmpty()) return {};
    QString n = toNative(p);
    if (QDir::isAbsolutePath(n) || (n.size() > 1 && n[1] == ':')) return QDir::cleanPath(n);
    return QDir::cleanPath(QDir(base).filePath(n));
}

QString programFolder() {
    const QDir exe(QCoreApplication::applicationDirPath());
    if (!exe.exists("data_template") && !exe.exists("lang") && QDir(exe.filePath("..")).exists("data_template"))
        return QDir::cleanPath(exe.filePath(".."));
    return exe.absolutePath();
}

} // namespace eld
