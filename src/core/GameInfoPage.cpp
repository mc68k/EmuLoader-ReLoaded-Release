#include "GameInfoPage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

namespace eld {

namespace {
QString esc(const QString& s) { return s.toHtmlEscaped(); }

// a document's line: its web links made links
QString docLine(const QString& line) {
    static const QRegularExpression url(QStringLiteral(R"((https?://[^\s<>"]+))"));
    QString out = esc(line);
    return out.replace(url, QStringLiteral("<a href=\"\\1\">\\1</a>"));
}

const char* kStyle = R"(
:root { --bg: #f4f4f6; --card: #ffffff; --text: #1d1d22; --muted: #6a6a75; --line: #e1e1e7; --accent: #2457c5;
        --good: #1f7a3d; --imperfect: #9a6a00; --bad: #b3261e; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #16161a; --card: #202027; --text: #ececf1; --muted: #9a9aa6; --line: #33333d; --accent: #7aa2ff;
          --good: #5fc27e; --imperfect: #e0b04a; --bad: #ff7b72; }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.5 "Segoe UI", system-ui, sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 48px; }
header h1 { margin: 0 0 4px; font-size: 26px; line-height: 1.25; }
header .sub { color: var(--muted); }
section { background: var(--card); border: 1px solid var(--line); border-radius: 10px; padding: 16px 20px; margin-top: 16px; }
h2 { margin: 0 0 10px; font-size: 17px; }
table { border-collapse: collapse; width: 100%; }
th, td { text-align: left; vertical-align: top; padding: 4px 8px 4px 0; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: normal; width: 34%; white-space: nowrap; }
tr:last-child th, tr:last-child td { border-bottom: none; }
.cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); gap: 0 32px; }
.good { color: var(--good); } .imperfect { color: var(--imperfect); } .bad { color: var(--bad); }
.pictures { display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 14px; }
figure { margin: 0; }
figure a { display: block; background: #000; border-radius: 6px; overflow: hidden; }
figure img { display: block; width: 100%; height: 200px; object-fit: contain; }
figcaption { color: var(--muted); font-size: 13px; margin-top: 4px; }
.doc { white-space: pre-wrap; font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 13px; overflow-wrap: anywhere; }
.version { color: var(--muted); font-size: 13px; margin-bottom: 8px; }
a { color: var(--accent); }
footer { color: var(--muted); font-size: 12px; margin-top: 20px; text-align: center; }
)";

void row(QString& out, const QString& label, const QString& value, const QString& cls = {}) {
    if (value.trimmed().isEmpty()) return;
    out += "<tr><th>" + esc(label) + "</th><td" + (cls.isEmpty() ? QString() : " class=\"" + cls + "\"") + ">" + esc(value) + "</td></tr>\n";
}

QString statusText(int s) {
    switch (s) {
    case 0: return GameInfoPage::tr("Good");
    case 1: return GameInfoPage::tr("Imperfect");
    case 2: return GameInfoPage::tr("Preliminary");
    default: return {};
    }
}
QString statusClass(int s) { return s == 0 ? "good" : s == 1 ? "imperfect" : s == 2 ? "bad" : QString(); }
} // namespace

QString GameInfoPage::file(const QString& folder, const MameGame& g) {
    const QDir d(folder);
    return g.isSoftware() ? d.filePath(g.softwareList + "/" + g.name + ".html") : d.filePath(g.name + ".html");
}

QString GameInfoPage::html(const Input& in) {
    const MameGame& g = in.game;
    QString out = "<!doctype html>\n<html lang=\"" + QLocale().bcp47Name() + "\">\n<head>\n<meta charset=\"utf-8\">\n"
                  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" +
                  esc(g.title.isEmpty() ? g.name : g.title) + "</title>\n<style>" + QString::fromLatin1(kStyle) + "</style>\n</head>\n<body>\n<main>\n";
    // the title, then what it is
    QStringList sub;
    if (!g.year.isEmpty()) sub << g.year;
    if (!g.manufacturer.isEmpty()) sub << g.manufacturer;
    sub << (g.isSoftware() ? g.softwareTitle : in.system);
    out += "<header><h1>" + esc(g.title.isEmpty() ? g.name : g.title) + "</h1><div class=\"sub\">" + esc(sub.join(QStringLiteral(" · "))) +
           "</div></header>\n";

    // the pictures: the title and the game first, as they come
    if (!in.pictures.isEmpty()) {
        out += "<section><h2>" + esc(tr("Pictures")) + "</h2><div class=\"pictures\">\n";
        for (const Picture& p : in.pictures) {
            const QString url = QUrl::fromLocalFile(p.file).toString(QUrl::FullyEncoded);
            const QString caption = p.parents ? tr("%1 (the parent's)").arg(p.title) : p.title;
            out += "<figure><a href=\"" + url + "\"><img src=\"" + url + "\" alt=\"" + esc(caption) + "\" loading=\"lazy\"></a><figcaption>" +
                   esc(caption) + "</figcaption></figure>\n";
        }
        out += "</div></section>\n";
    }

    // the details, in two columns
    QString about, emulation;
    row(about, tr("Name"), g.isSoftware() ? g.softwareList + "\\" + g.name : g.name);
    row(about, tr("Clone of"), g.cloneOf.isEmpty() ? QString() : (in.parentTitle.isEmpty() ? g.cloneOf : in.parentTitle + " [" + g.cloneOf + "]"));
    row(about, tr("Year"), g.year);
    row(about, tr("Manufacturer"), g.manufacturer);
    if (g.isSoftware()) {
        row(about, tr("Software List"), g.softwareTitle + " [" + g.softwareList + "]");
        row(about, tr("Alternative Title"), g.altTitle);
        row(about, tr("Media"), g.media);
        row(about, tr("Usage"), g.usage);
        row(about, tr("Compatibility"), g.compat);
        row(about, tr("Notes"), g.notes);
    } else {
        row(about, tr("Category"), g.category);
        row(about, tr("Version Added"), g.versionAdded);
        row(about, tr("Driver"), g.driver);
        row(about, tr("BIOS"), g.bios);
        row(about, tr("Language"), g.language);
        if (g.players > 0) row(about, tr("Players"), g.playersIni.isEmpty() ? QString::number(g.players) : g.playersIni);
        if (g.buttons > 0) row(about, tr("Buttons"), QString::number(g.buttons));
        row(about, tr("Controls"), QString(g.controls).replace(',', QStringLiteral(", ")));
        const QString screen = g.screenType == 0 ? tr("Raster") : g.screenType == 1 ? tr("Vector") : g.screenType == 2 ? tr("LCD") : QString();
        QStringList sc;
        if (!screen.isEmpty()) sc << screen;
        if (g.orientation == 0) sc << tr("Horizontal");
        if (g.orientation == 1) sc << tr("Vertical");
        if (!g.resolution.isEmpty()) sc << g.resolution;
        if (!g.refresh.isEmpty()) { // 60.000000 -> 60, 59.185606 stays
            QString hz = g.refresh;
            if (hz.contains('.')) {
                while (hz.endsWith('0')) hz.chop(1);
                if (hz.endsWith('.')) hz.chop(1);
            }
            sc << hz + " Hz";
        }
        row(about, tr("Screen"), sc.join(QStringLiteral(", ")));
        if (!g.softwareLists.isEmpty()) row(about, tr("Software Lists"), g.softwareLists.join(QStringLiteral(", ")));
    }
    row(emulation, tr("Driver Status"), statusText(g.driverStatus), statusClass(g.driverStatus));
    row(emulation, tr("Emulation"), statusText(g.emuStatus), statusClass(g.emuStatus));
    row(emulation, tr("Graphics"), statusText(g.graphicStatus), statusClass(g.graphicStatus));
    row(emulation, tr("Color"), statusText(g.colorStatus), statusClass(g.colorStatus));
    row(emulation, tr("Sound"), statusText(g.soundStatus), statusClass(g.soundStatus));
    row(emulation, tr("Protection"), statusText(g.protectionStatus), statusClass(g.protectionStatus));
    if (g.saveState >= 0) row(emulation, tr("Save State"), g.saveState == 1 ? tr("Supported") : tr("Unsupported"), g.saveState == 1 ? "good" : "bad");
    row(emulation, tr("Your Set"), g.setStatus == 0 ? tr("Available") : g.setStatus == 1 ? tr("Incomplete") : tr("Missing"),
        g.setStatus == 0 ? "good" : g.setStatus == 1 ? "imperfect" : "bad");
    if (g.played > 0) row(emulation, tr("Played"), g.played == 1 ? tr("Once") : tr("%1 times").arg(QLocale().toString(g.played)));
    row(emulation, tr("Last Played"), in.lastPlayed);
    row(emulation, tr("Playtime"), in.playtime);
    row(emulation, tr("Emulator"), in.emulator);
    out += "<section class=\"cols\"><div><h2>" + esc(tr("About")) + "</h2><table>\n" + about + "</table></div>";
    out += "<div><h2>" + esc(tr("Emulation")) + "</h2><table>\n" + emulation + "</table></div></section>\n";

    // the clones
    if (!in.clones.isEmpty()) {
        out += "<section><h2>" + esc(tr("Clones")) + "</h2><table>\n";
        for (const MameGame& c : in.clones)
            row(out, c.name, c.title + (c.year.isEmpty() ? QString() : " (" + c.year + ")"));
        out += "</table></section>\n";
    }

    // the game documents
    for (const Doc& d : in.docs) {
        out += "<section><h2>" + esc(d.title) + "</h2>\n";
        if (!d.version.isEmpty()) out += "<div class=\"version\">" + docLine(d.version) + "</div>\n";
        QStringList lines;
        for (const QString& l : d.lines) lines << docLine(l);
        out += "<div class=\"doc\">" + lines.join('\n') + "</div></section>\n";
    }
    out += "<footer>" + esc(tr("Made by %1 from your files, %2").arg(in.generator, QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat))) +
           "</footer>\n</main>\n</body>\n</html>\n";
    return out;
}

bool GameInfoPage::write(const QString& file, const Input& in) {
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = html(in).toUtf8();
    return f.write(data) == data.size();
}

} // namespace eld
