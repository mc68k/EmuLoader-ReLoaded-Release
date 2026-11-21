// ScriptFonts - fonts for letters a chosen font doesn't have, and a language's own font and size.
//
// Most fonts (Segoe UI, the games list's and the header's own fonts) have no Thai, Japanese,
// Korean or Chinese letters. Left to itself, the system shows them in whatever font it finds,
// often an old, thin one (Tahoma, MS UI Gothic) that reads small. setup() gives every installed
// font the system's modern font for each script as its stand-in (Leelawadee UI for Thai, Yu Gothic
// UI for Japanese ...), the language's own script first: one call at startup covers every window.
//
// A language file can also ask, under [Language], for a font for EmuLoader's windows and menus
// (Font=) and larger text (FontSize=+1). The size is added where a font is shown (shown()), never
// to a font that's saved, so the user's own font sizes stay as they set them.
#pragma once

#include <QFont>
#include <QString>
#include <QStringList>

namespace eld {

namespace ScriptFonts {

// language: the language's code ("th", "ja", "zh_TW"; "" for English); font: its Font= ("" for the
// usual fonts); sizeDelta: its FontSize= in points (0 for none). Call once, before any window.
void setup(const QString& language, const QString& font, int sizeDelta);

// The stand-in fonts, in the order they're tried (only fonts installed here)
QStringList fallbacks();

// The language's font for the windows and menus ("" when it asks for none)
QString uiFamily();
// Whether a form's font is one of the usual interface fonts the language's font replaces
bool isInterfaceFamily(const QString& family);

int sizeDelta(); // points
// f as shown: the language's extra size added
QFont shown(QFont f);
// A pixel size with the language's extra size added
int shownPixels(int px);

// [Language] FontSize=: "+1", "-1" or "1" (all points to add); 0 when empty or not a number
int parseSize(const QString& s);

} // namespace ScriptFonts

} // namespace eld
