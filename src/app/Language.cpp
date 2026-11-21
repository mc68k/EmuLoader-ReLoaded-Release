#include "Language.h"

#include "ScriptFonts.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <functional>

namespace eld {

// Escapes: \\ backslash, \n new line, \t tab, \= an equals sign in the English text, and a
// backslash in front of a character that would otherwise be lost or misread: a space at the
// start or end of the text ("\ "), or ; or [ at the start of a line.
QString LanguageFile::escape(const QString& s, bool key) {
    QString out;
    out.reserve(s.size() + 8);
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') continue;
        else if (c == '=' && key) out += "\\=";
        else if (c == ' ' && (i == 0 || i == s.size() - 1)) out += "\\ "; // editors drop these
        else if (i == 0 && key && (c == ';' || c == '[')) out += QString("\\") + c; // comment / section
        else out += c;
    }
    return out;
}

QString LanguageFile::unescape(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i + 1 >= s.size()) break; // a lone backslash at the end: nothing to escape
        QChar n = s[++i];
        if (n == 'n') out += '\n';
        else if (n == 't') out += '\t';
        else out += n; // \\ \= \; \[ and "\ "
    }
    return out;
}

// The first '=' that isn't escaped.
static int separator(const QString& line) {
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') { ++i; continue; }
        if (line[i] == '=') return i;
    }
    return -1;
}

static void parse(const QString& file, const std::function<void(const QString&, const QString&, const QString&)>& f) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return;
    QByteArray data = in.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    QString section;
    for (QString line : QString::fromUtf8(data).split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty() || line.startsWith(';')) continue;
        if (line.startsWith('[') && line.trimmed().endsWith(']')) {
            section = line.trimmed().mid(1).chopped(1);
            continue;
        }
        int eq = separator(line);
        if (eq <= 0) continue;
        f(section, LanguageFile::unescape(line.left(eq)), LanguageFile::unescape(line.mid(eq + 1)));
    }
}

LanguageFile::Info LanguageFile::readInfo(const QString& file) {
    Info i;
    i.file = file;
    i.id = QFileInfo(file).completeBaseName();
    // only the [Language] section, near the top: a whole translation is several hundred KB, and the
    // language list reads every file
    QFile in(file);
    if (in.open(QIODevice::ReadOnly)) {
        bool inLanguage = false;
        for (int n = 0; n < 400 && !in.atEnd(); ++n) {
            QString line = QString::fromUtf8(in.readLine()).trimmed();
            if (n == 0 && line.startsWith(QChar(0xFEFF))) line.remove(0, 1);
            if (line.isEmpty() || line.startsWith(';')) continue;
            if (line.startsWith('[') && line.endsWith(']')) {
                if (inLanguage) break; // the next section
                inLanguage = line == "[Language]";
                continue;
            }
            if (!inLanguage) continue;
            const int eq = separator(line);
            if (eq <= 0) continue;
            const QString k = unescape(line.left(eq)), v = unescape(line.mid(eq + 1));
            if (k == "Name") i.name = v;
            else if (k == "Locale") i.locale = v;
            else if (k == "Font") i.font = v.trimmed();
            else if (k == "FontSize") i.fontSize = ScriptFonts::parseSize(v);
        }
    }
    // Files are named by language code (de.ini, pt_BR.ini): then Locale= may be left out.
    static const QRegularExpression code("^[a-z]{2,3}([_-][A-Z]{2})?$");
    if (i.locale.isEmpty() && code.match(i.id).hasMatch()) i.locale = i.id;
    if (i.name.isEmpty() && !i.locale.isEmpty()) {
        QString n = QLocale(i.locale).nativeLanguageName();
        if (!n.isEmpty()) i.name = n.left(1).toUpper() + n.mid(1);
    }
    if (i.name.isEmpty()) i.name = i.id;
    return i;
}

bool LanguageFile::Info::matches(const QString& setting) const {
    if (setting.isEmpty()) return false;
    if (id.compare(setting, Qt::CaseInsensitive) == 0 || name.compare(setting, Qt::CaseInsensitive) == 0) return true;
    if (locale.isEmpty()) return false;
    QLocale l(locale);
    // "English" is the built-in (US) English, not en_GB.ini
    if (l.language() == QLocale::English && locale.contains(QRegularExpression("[_-]"))) return false;
    return l.language() != QLocale::C && QLocale::languageToString(l.language()).compare(setting, Qt::CaseInsensitive) == 0;
}

QString LanguageFile::systemDefault(const QList<Info>& langs) {
    QLocale sys = QLocale::system();
    QString full = sys.name();                 // "pt_BR"
    QString lang = full.section('_', 0, 0);    // "pt"
    for (const Info& i : langs) // the exact variant first
        if (!i.file.isEmpty() && QString(i.locale).replace('-', '_').compare(full, Qt::CaseInsensitive) == 0) return i.id;
    // An English system that isn't en_GB (en_US, en_AU ...) keeps the built-in (US) English
    // rather than British English.
    if (lang.compare("en", Qt::CaseInsensitive) == 0) return "English";
    for (const Info& i : langs)
        if (!i.file.isEmpty() && i.locale.section(QRegularExpression("[_-]"), 0, 0).compare(lang, Qt::CaseInsensitive) == 0) return i.id;
    return "English";
}

bool LanguageFile::load(const QString& file) {
    texts_.clear();
    if (!QFileInfo::exists(file)) return false;
    info_ = readInfo(file);
    parse(file, [this](const QString& sec, const QString& k, const QString& v) {
        if (sec != "Language" && !v.isEmpty()) texts_[sec].insert(k, v);
    });
    return true;
}

// The section a text of the program's context is in (LanguageSections.inc): "Favorites Manager"
// for both eld::FavoritesManagerDialog and FormFavoritesManager
static QString sectionOf(const QString& ctx) {
    static const QHash<QString, QString> map = [] {
        struct E { const char* ctx; const char* section; };
        static const E all[] = {
#include "LanguageSections.inc"
        };
        QHash<QString, QString> m;
        for (const E& e : all) m.insert(QString::fromLatin1(e.ctx), QString::fromLatin1(e.section));
        return m;
    }();
    return map.value(ctx, ctx);
}

QString LanguageFile::translate(const char* context, const char* sourceText, const char*, int) const {
    if (!sourceText || !*sourceText) return {};
    QString src = QString::fromUtf8(sourceText);
    QString ctx = QString::fromUtf8(context);
    const QString section = sectionOf(ctx);
    auto find = [this, &ctx, &section](const QString& text) -> QString {
        // its section, the program's name for it (files made before the sections had names), [Common]
        for (const QString& sec : {section, ctx, QStringLiteral("Common")}) {
            auto c = texts_.constFind(sec);
            if (c == texts_.cend()) continue;
            if (auto t = c->constFind(text); t != c->cend()) return *t;
        }
        return {};
    };
    QString t = find(src);
    if (!t.isEmpty()) return t;
    // Some of the original's labels are padded with spaces for their layout ("  System Title").
    // The language file lists them without the spaces; the spaces are put back around the
    // translation.
    QString core = src.trimmed();
    if (core.isEmpty() || core == src) return {}; // not translated: Qt shows the English text
    t = find(core);
    if (t.isEmpty()) return {};
    int lead = src.indexOf(core);
    return src.left(lead) + t + src.mid(lead + core.size());
}

QList<LanguageFile::Info> LanguageFile::available(const QStringList& folders) {
    QMap<QString, Info> byId;
    for (const QString& dir : folders)
        for (const QFileInfo& fi : QDir(dir).entryInfoList({"*.ini"}, QDir::Files, QDir::Name))
            byId.insert(fi.completeBaseName().toLower(), readInfo(fi.absoluteFilePath()));
    QList<Info> out;
    if (byId.contains("english")) out << byId.take("english");
    else out << Info{QString(), "English", "English", "en", QString()}; // built in
    QList<Info> rest = byId.values();
    std::sort(rest.begin(), rest.end(), [](const Info& a, const Info& b) { return a.name.localeAwareCompare(b.name) < 0; });
    return out + rest;
}

// The language's fonts, for the whole application (before any window)
static void installFonts(const QString& language, const QString& font, int sizeDelta) {
    ScriptFonts::setup(language, font, sizeDelta);
    if (ScriptFonts::uiFamily().isEmpty() && ScriptFonts::sizeDelta() == 0) return;
    QFont f = QApplication::font();
    if (!ScriptFonts::uiFamily().isEmpty()) f.setFamily(ScriptFonts::uiFamily());
    QApplication::setFont(ScriptFonts::shown(f));
}

QString installLanguage(const QString& id, const QStringList& folders, const QString& qtTranslationsDir) {
    if (id.isEmpty() || id.compare("English", Qt::CaseInsensitive) == 0) {
        installFonts({}, {}, 0);
        return "English";
    }
    for (const LanguageFile::Info& i : LanguageFile::available(folders)) {
        if (i.file.isEmpty() || !i.matches(id)) continue;
        auto* lf = new LanguageFile;
        lf->load(i.file);
        installFonts(i.locale.isEmpty() ? i.id : i.locale, i.font, i.fontSize);
        QCoreApplication::installTranslator(lf);
        if (!i.locale.isEmpty()) {
            // Qt's own texts (standard buttons, file and color dialogs).
            auto* qt = new QTranslator;
            QStringList dirs{qtTranslationsDir, QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
            for (const QString& d : dirs)
                if (qt->load(QLocale(i.locale), "qtbase", "_", d)) {
                    QCoreApplication::installTranslator(qt);
                    break;
                }
        }
        return i.id;
    }
    installFonts({}, {}, 0);
    return "English";
}

QString chooseLanguage(const QList<LanguageFile::Info>& langs, const QString& current, QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle("EmuLoader - Language");
    auto* lay = new QVBoxLayout(&dlg);
    auto* label = new QLabel("Choose the language for EmuLoader ReLoaded.\nYou can change it later in Settings > General.", &dlg);
    lay->addWidget(label);
    auto* list = new QListWidget(&dlg);
    for (const LanguageFile::Info& i : langs) {
        auto* it = new QListWidgetItem(i.name, list);
        it->setData(Qt::UserRole, i.id);
        if (i.id.compare(current, Qt::CaseInsensitive) == 0) list->setCurrentItem(it);
    }
    if (!list->currentItem() && list->count()) list->setCurrentRow(0);
    lay->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    lay->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    dlg.resize(360, 320);
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return {};
    return list->currentItem()->data(Qt::UserRole).toString();
}

} // namespace eld
