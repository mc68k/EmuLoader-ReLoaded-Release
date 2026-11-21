#include "MultiSlot.h"

#include "TextFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace eld {

namespace {
// SoftListGetEntryValue: <tag>value/> in a line
QString tagValue(const QString& line, const QString& tag) {
    const QString open = '<' + tag + '>';
    const int p = line.indexOf(open, 0, Qt::CaseInsensitive);
    if (p < 0) return {};
    const int from = p + int(open.size());
    const int end = line.indexOf(QStringLiteral("/>"), from);
    return end < 0 ? QString() : line.mid(from, end - from).trimmed();
}

// The file's lines, and the [section]'s first line and end (-1 when it isn't there)
struct Lines {
    QStringList lines;
    bool utf8 = true;
    bool load(const QString& file) { QString t; if (!readTextFile(file, t, &utf8)) return false; lines = t.split('\n'); for (QString& l : lines) if (l.endsWith('\r')) l.chop(1); return true; }
    int section(const QString& name) const {
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].trimmed().compare('[' + name + ']', Qt::CaseInsensitive) == 0) return i;
        return -1;
    }
    int sectionEnd(int start) const {
        int i = start + 1;
        while (i < lines.size() && !lines[i].trimmed().startsWith('[')) ++i;
        return i;
    }
};
} // namespace

QString MultiSlot::machineOf(const MameGame& g) {
    if (g.isSoftware()) return {};
    const QString drv = QFileInfo(g.driver).completeBaseName().toLower();
    if (g.bios.compare(QLatin1String("neogeo"), Qt::CaseInsensitive) == 0 || drv == QLatin1String("neopcb")) return QStringLiteral("neogeo");
    if (drv == QLatin1String("stv") || drv == QLatin1String("megatech") || drv == QLatin1String("playch10")) return drv;
    return {};
}

int MultiSlot::maxSlots(const QString& machine) {
    if (machine == QLatin1String("neogeo")) return 6;
    if (machine == QLatin1String("stv")) return 4;
    if (machine == QLatin1String("megatech")) return 8;
    if (machine == QLatin1String("playch10")) return 10;
    return 0;
}

QString MultiSlot::title(const QString& machine) {
    if (machine == QLatin1String("neogeo")) return QStringLiteral("Neo-Geo MVS (Multi Video System)");
    if (machine == QLatin1String("stv")) return QStringLiteral("Sega Titan Video Game System (ST-V)");
    if (machine == QLatin1String("megatech")) return QStringLiteral("Sega Mega-Tech System");
    if (machine == QLatin1String("playch10")) return QStringLiteral("Nintendo PlayChoice-10");
    return machine;
}

QString MultiSlot::cartFile(const QString& gamesFolder, const QString& systemBase, const QString& machine) {
    return QDir(gamesFolder).filePath(systemBase + "_multicart_" + machine + ".ini");
}

bool MultiSlot::createCartList(const QString& xmlFile, const QString& file) {
    QFile f(xmlFile);
    if (!f.open(QFile::ReadOnly)) return false;
    QStringList names;
    QXmlStreamReader x(&f);
    while (!x.atEnd()) {
        if (x.readNext() != QXmlStreamReader::StartElement || x.name() != QLatin1String("software")) continue;
        const auto a = x.attributes();
        if (a.value("supported") != QLatin1String("no") && !a.value("name").isEmpty()) names << a.value("name").toString();
    }
    if (x.hasError() && names.isEmpty()) return false;
    QFile::remove(file);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return writeTextFile(file, names.join("\r\n") + (names.isEmpty() ? "" : "\r\n"), false);
}

QSet<QString> MultiSlot::loadCartList(const QString& file) {
    QSet<QString> out;
    QString t;
    if (!readTextFile(file, t)) return out;
    for (const QString& l : t.split('\n'))
        if (const QString n = l.trimmed(); !n.isEmpty()) out.insert(n);
    return out;
}

QVector<MameGame> MultiSlot::slotGames(const QString& machine, const QVector<MameGame>& selected, const QSet<QString>& carts) {
    QVector<MameGame> out;
    const int max = maxSlots(machine);
    if (!max) return out;
    for (const MameGame& g : selected) {
        if (int(out.size()) >= max) break;
        if (g.isSoftware() || !g.have() || machineOf(g) != machine) continue;
        if (g.name == machine || (machine == QLatin1String("stv") && g.name == QLatin1String("stvbios"))) continue; // the machine itself
        if (hasCartList(machine) && !carts.contains(g.name)) continue;
        out << g;
    }
    return out;
}

QVector<MultiSlot::Board> MultiSlot::boards(const QString& machinesFile) {
    QVector<Board> out;
    Lines f;
    if (!f.load(machinesFile)) return out;
    const int s = f.section(QStringLiteral("neogeo"));
    if (s < 0) return out;
    for (int i = s + 1; i < f.sectionEnd(s); ++i) {
        const QString l = f.lines[i].trimmed();
        if (l.isEmpty() || l.startsWith(QLatin1String("//")) || l.startsWith(';')) continue;
        const int eq = l.indexOf('=');
        if (eq <= 0) continue;
        const QString v = l.mid(eq + 1);
        if (v.trimmed().isEmpty()) continue;
        Board b;
        b.name = l.left(eq).trimmed();
        b.title = tagValue(v, "title");
        b.year = tagValue(v, "year");
        b.slotCount = qBound(1, tagValue(v, "slots").toInt(), maxSlots("neogeo"));
        b.chipset = tagValue(v, "videochipset");
        b.notes = tagValue(v, "notes");
        if (b.title.isEmpty()) b.title = b.name;
        out << b;
    }
    return out;
}

QString MultiSlot::lastBoard(const QString& machinesFile) {
    Lines f;
    if (f.load(machinesFile)) {
        const int s = f.section(QStringLiteral("lastused"));
        if (s >= 0)
            for (int i = s + 1; i < f.sectionEnd(s); ++i) {
                const QString l = f.lines[i].trimmed();
                if (l.startsWith(QLatin1String("neogeo="), Qt::CaseInsensitive) && l.size() > 7) return l.mid(7).trimmed();
            }
    }
    return QStringLiteral("neogeo");
}

bool MultiSlot::setLastBoard(const QString& machinesFile, const QString& board, const QString& from) {
    Lines f;
    if (!f.load(machinesFile) && !(from.isEmpty() || f.load(from))) f.lines.clear();
    while (!f.lines.isEmpty() && f.lines.last().trimmed().isEmpty()) f.lines.removeLast();
    int s = f.section(QStringLiteral("lastused"));
    if (s < 0) {
        if (!f.lines.isEmpty()) f.lines << QString();
        f.lines << QStringLiteral("[lastused]");
        s = int(f.lines.size()) - 1;
    }
    const QString line = QStringLiteral("neogeo=") + board;
    bool done = false;
    for (int i = s + 1; i < f.sectionEnd(s) && !done; ++i)
        if (f.lines[i].trimmed().startsWith(QLatin1String("neogeo="), Qt::CaseInsensitive)) {
            f.lines[i] = line;
            done = true;
        }
    if (!done) f.lines.insert(s + 1, line);
    QDir().mkpath(QFileInfo(machinesFile).absolutePath());
    return writeTextFile(machinesFile, f.lines.join("\r\n") + "\r\n", f.utf8);
}

QString MultiSlot::machineToRun(const QString& machine, const QString& board) {
    if (machine == QLatin1String("neogeo")) return board.isEmpty() ? machine : board;
    if (machine == QLatin1String("stv")) return QStringLiteral("stvbios");
    return machine;
}

QStringList MultiSlot::commandLine(const QString& machineToRun, const QStringList& games) {
    QStringList out{machineToRun};
    for (int i = 0; i < games.size(); ++i) out << QStringLiteral("-cart%1").arg(i + 1) << games[i];
    return out;
}

} // namespace eld
