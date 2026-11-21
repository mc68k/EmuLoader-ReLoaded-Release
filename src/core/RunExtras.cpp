#include "RunExtras.h"
#include "TextFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace eld {

namespace CustomParams {

QString folder(const QString& dataDir, const QString& systemBase) {
    return QDir(dataDir).filePath("arcade/custom_params/" + systemBase + "/");
}

// TStringList.Values['custom_params']: the first line "custom_params=..." (name ignores case).
QString read(const QString& file) {
    QString text;
    if (!readTextFile(file, text)) return {};
    for (const QString& line : text.split('\n')) {
        int eq = line.indexOf('=');
        if (eq > 0 && line.left(eq).compare("custom_params", Qt::CaseInsensitive) == 0)
            return line.mid(eq + 1).trimmed();
    }
    return {};
}

bool write(const QString& file, const QString& params) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, "custom_params=" + params.trimmed() + "\r\n", false);
}

QString fileFor(const QString& folder, const QString& game, const QString& parent) {
    QDir d(folder);
    if (QFileInfo::exists(d.filePath(game + ".txt"))) return d.filePath(game + ".txt");
    if (!parent.isEmpty() && QFileInfo::exists(d.filePath(parent + ".txt"))) return d.filePath(parent + ".txt");
    return {};
}

QStringList arguments(const QString& game, const QString& params) {
    QString p = params.trimmed();
    static const QRegularExpression tag("<game(name)?>", QRegularExpression::CaseInsensitiveOption);
    if (p.contains(tag)) return QProcess::splitCommand(QString(p).replace(tag, game));
    QStringList args{game};
    if (!p.isEmpty()) args << QProcess::splitCommand(p);
    return args;
}

} // namespace CustomParams

namespace MemoryCards {

QString supportFile(const QString& gamesFolder, const QString& systemBase) {
    return QDir(gamesFolder).filePath(systemBase + "_memcard_support.ini");
}

QString lastUsedFile(const QString& dataDir, const QString& systemBase, const QString& section) {
    if (section.isEmpty()) return {};
    return QDir(dataDir).filePath("arcade/memcard_lastused/" + systemBase + "/" + section + ".ini");
}

bool isNeoGeo(const QString& bios, const QString& driver) {
    if (bios.compare("neogeo", Qt::CaseInsensitive) == 0) return true;
    QString base = QFileInfo(QString(driver).replace('\\', '/')).completeBaseName();
    return base.compare("neopcb", Qt::CaseInsensitive) == 0;
}

// TMemIniFile.ReadSectionValues: the section's lines, without empty ones and comments.
MemoryCardSlots read(const QString& file, const QString& game, bool neoGeo) {
    MemoryCardSlots out;
    QString text;
    if (!readTextFile(file, text)) return out;
    const QString want = neoGeo ? QStringLiteral("neogeo") : game;
    bool in = false;
    QStringList lines;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            if (in) break;
            in = line.mid(1, line.size() - 2).compare(want, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (in && !line.isEmpty() && !line.startsWith(';')) lines << line;
    }
    if (lines.size() < 2) return out;
    out.section = want;
    out.extensions = lines.takeFirst().split(';', Qt::SkipEmptyParts);
    out.options = lines.mid(0, 4);
    return out;
}

bool write(const QString& file, const QList<MemoryCardSlots>& machines) {
    QString out;
    for (const MemoryCardSlots& m : machines) {
        if (!out.isEmpty()) out += "\r\n";
        out += "[" + m.section + "]\r\n" + m.extensions.join(';') + "\r\n";
        for (const QString& s : m.options) out += s + "\r\n";
    }
    if (out.isEmpty()) return QFile::remove(file) || !QFileInfo::exists(file);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, out, false);
}

} // namespace MemoryCards

} // namespace eld
