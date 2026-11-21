// DiagnosticLog - Preferences > Enable Diagnostic Log: what EmuLoader does, a line each, to find out why something went wrong
// on someone's computer. EmuLoaderRL\diagnostic.log:
//   2026-11-08 01:30:12.345 [pid=1234 tid=5678] [process] Launching: mame.exe mslug
// Categories as the original's: session, games, process, preferences, dialog, filters ...;
// "qt" for Qt's own warnings. At 5 MB the log becomes diagnostic.log.1 and a new one starts.
// The --debug-log switch turns it on whatever the setting says ([Preferences] DiagnosticLog).
#pragma once

#include <QString>
#include <functional>

namespace eld::DiagnosticLog {

// The log's file and the program's version; on when `preference` or `commandLine` is (once
// --debug-log turned it on, it stays on).
void open(const QString& file, const QString& version, bool preference, bool commandLine);
void setPreference(bool on);        // Preferences closed
bool enabled();
QString file();
void write(const QString& category, const QString& message);
// Called (once) when the log can't be written: logging stops (the original's ReportFailure)
void onFailure(const std::function<void(const QString& file, const QString& error)>& f);
void close();                       // the session's last line ("ended"); no more lines after it

} // namespace eld::DiagnosticLog
