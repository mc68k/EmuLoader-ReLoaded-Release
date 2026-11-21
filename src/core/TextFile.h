// Text file helpers. EmuLoader's Delphi 7 heritage means many of its files are
// Windows-1252 ("ANSI"), while newer ones may be UTF-8 (with or without BOM).
// readTextFile detects which; writeTextFile writes back the same encoding.
#pragma once

#include <QString>

namespace eld {

bool readTextFile(const QString& path, QString& out, bool* wasUtf8 = nullptr);
bool writeTextFile(const QString& path, const QString& text, bool utf8 = true, bool bom = false);

// Delphi paths use backslashes and may be relative to a base folder.
QString resolvePath(const QString& base, const QString& p);
QString toNative(const QString& p);

// The program's folder (EmuLoader-ReLoaded.exe, lang\, data_template\, the guide): the
// executable's folder, or the one above it for a program in a subfolder.
QString programFolder();

} // namespace eld
