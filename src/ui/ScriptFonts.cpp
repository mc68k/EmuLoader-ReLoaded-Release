#include "ScriptFonts.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace eld::ScriptFonts {

namespace {

QStringList gFallbacks;
QString gUiFamily;
int gDelta = 0;

// The system's own fonts per script, best first. Only the installed ones are used, so the lists
// can name every system's: Windows, macOS, then Linux (Noto).
QStringList thai() { return {"Leelawadee UI", "Leelawadee", "Thonburi", "Noto Sans Thai UI", "Noto Sans Thai", "Tahoma"}; }
QStringList japanese() { return {"Yu Gothic UI", "Meiryo UI", "Hiragino Sans", "Noto Sans CJK JP", "Noto Sans JP", "MS UI Gothic"}; }
QStringList korean() { return {"Malgun Gothic", "Apple SD Gothic Neo", "Noto Sans CJK KR", "Noto Sans KR"}; }
QStringList chineseSimplified() { return {"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC", "Noto Sans SC"}; }
QStringList chineseTraditional() { return {"Microsoft JhengHei UI", "Microsoft JhengHei", "PingFang TC", "Noto Sans CJK TC", "Noto Sans TC"}; }
QStringList others() {
    return {"Nirmala UI", "Noto Sans Devanagari", "Noto Sans Bengali", "Noto Sans Tamil", // India's scripts
            "Leelawadee UI", "Noto Sans Lao", "Noto Sans Khmer",                         // Lao, Khmer
            "Ebrima", "Noto Sans Ethiopic", "Myanmar Text", "Noto Sans Myanmar",
            "Segoe UI Historic", "Segoe UI Symbol", "Segoe UI Emoji", "Apple Symbols", "Noto Sans Symbols",
            "Noto Sans Symbols2", "Noto Color Emoji"};
}

} // namespace

void setup(const QString& language, const QString& font, int sizeDelta) {
    gDelta = sizeDelta;
    const QString lang = language.toLower().replace('-', '_');
    // the language's own script first; Han letters take the shapes of the first CJK font found
    QList<QStringList> order;
    if (lang.startsWith("th")) order = {thai(), japanese(), chineseSimplified(), chineseTraditional(), korean()};
    else if (lang.startsWith("ja")) order = {japanese(), chineseSimplified(), chineseTraditional(), korean(), thai()};
    else if (lang.startsWith("ko")) order = {korean(), japanese(), chineseSimplified(), chineseTraditional(), thai()};
    else if (lang == "zh_tw" || lang == "zh_hk" || lang == "zh_mo" || lang.startsWith("zh_hant"))
        order = {chineseTraditional(), chineseSimplified(), japanese(), korean(), thai()};
    else if (lang.startsWith("zh")) order = {chineseSimplified(), chineseTraditional(), japanese(), korean(), thai()};
    else order = {japanese(), chineseSimplified(), chineseTraditional(), korean(), thai()};
    order << others();

    const QStringList installed = QFontDatabase::families();
    QSet<QString> have;
    for (const QString& f : installed) have.insert(f.toLower());
    QStringList list;
    if (!font.isEmpty() && have.contains(font.toLower())) list << font;
    for (const QStringList& script : order)
        for (const QString& f : script)
            if (have.contains(f.toLower()) && !list.contains(f, Qt::CaseInsensitive)) list << f;
    gFallbacks = list;
    gUiFamily = !font.isEmpty() && have.contains(font.toLower()) ? font : QString();

    // every installed font: its own letters first, then the stand-ins (and the application's font
    // and the usual generic names, which may not be an installed font's name)
    QStringList families = installed;
    for (const QString& f : {QGuiApplication::font().family(), QString("Sans Serif"), QString("MS Shell Dlg 2")})
        if (!families.contains(f, Qt::CaseInsensitive)) families << f;
    for (const QString& f : families) {
        QFont::removeSubstitutions(f); // (a setup before: another language)
        QStringList subs;
        for (const QString& s : list)
            if (s.compare(f, Qt::CaseInsensitive) != 0) subs << s;
        if (!subs.isEmpty()) QFont::insertSubstitutions(f, subs);
    }
}

QStringList fallbacks() { return gFallbacks; }

QString uiFamily() { return gUiFamily; }

bool isInterfaceFamily(const QString& family) {
    static const QStringList ui{"Segoe UI", "Tahoma", "MS Sans Serif", "Microsoft Sans Serif", "MS Shell Dlg",
                                "MS Shell Dlg 2", "Noto Sans", "DejaVu Sans"};
    return ui.contains(family, Qt::CaseInsensitive);
}

int sizeDelta() { return gDelta; }

QFont shown(QFont f) {
    if (gDelta == 0) return f;
    if (f.pointSizeF() > 0) f.setPointSizeF(std::max(4.0, f.pointSizeF() + gDelta));
    else if (f.pixelSize() > 0) f.setPixelSize(shownPixels(f.pixelSize()));
    return f;
}

int shownPixels(int px) {
    if (gDelta == 0 || px <= 0) return px;
    return std::max(5, px + int(std::lround(gDelta * 96.0 / 72.0)));
}

int parseSize(const QString& s) {
    QString t = s.trimmed();
    if (t.startsWith('+')) t.remove(0, 1);
    bool ok = false;
    const int n = t.toInt(&ok);
    return ok ? std::clamp(n, -4, 8) : 0;
}

} // namespace eld::ScriptFonts
