// Language - user-editable translations (lang\<Name>.ini).
//
// A language file is plain UTF-8 text. Each section is a translation context (a window such as
// [FormMain], a part of the code such as [eld::MainWindow], or [Menus]); each line is
// "English text=translation". A missing or empty translation shows the English text, so a
// partial translation works. [Common] translates a text in every context at once.
//
//   ; EmuLoader ReLoaded language file
//   [Language]
//   Name=Deutsch
//   Locale=de
//   [Menus]
//   Add To Favorites=Zu Favoriten hinzufügen
//
// Escapes in keys and values: \n new line, \t tab, \\ backslash, \= equals sign; a key starting
// with ; or [ is written with a backslash in front.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QTranslator>

class QWidget;

namespace eld {

class LanguageFile : public QTranslator {
public:
    struct Info {
        QString file;    // full path
        QString id;      // file name without .ini: the language code ("de", "pt_BR"), stored in EmuLoader.ini
        QString name;    // [Language] Name, shown in the lists
        QString locale;  // [Language] Locale, or else the file name ("de"), for Qt's own dialogs
        QString font;    // [Language] Font: a font for EmuLoader's windows and menus ("" for the usual)
        int fontSize = 0; // [Language] FontSize: points added to all of EmuLoader's text ("+1")
        // Whether this is the language asked for: by file name, by shown name, or by the language's
        // English name ("French"), so a setting made before the file was renamed still finds it.
        bool matches(const QString& setting) const;
    };

    // The language that suits the system (Windows display language), or English.
    static QString systemDefault(const QList<Info>& langs);

    bool load(const QString& file);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return texts_.isEmpty(); }
    const Info& info() const { return info_; }

    // The language files in the program's and the data folder's lang\ folders (the data
    // folder's win when both have one), English first.
    static QList<Info> available(const QStringList& folders);
    static Info readInfo(const QString& file);

    static QString escape(const QString& s, bool key);
    static QString unescape(const QString& s);

private:
    Info info_;
    QHash<QString, QHash<QString, QString>> texts_; // context -> English -> translation
};

// The first-run language list (English and the language files found). Returns the chosen id,
// or an empty string when it's closed.
QString chooseLanguage(const QList<LanguageFile::Info>& langs, const QString& current, QWidget* parent = nullptr);

// Loads the chosen language (and Qt's own translations for it) into the application, with its fonts
// (ScriptFonts: the stand-ins for other scripts, its Font= and FontSize=).
// Returns the language in use; "English" when the file is missing.
QString installLanguage(const QString& id, const QStringList& folders, const QString& qtTranslationsDir);

} // namespace eld
