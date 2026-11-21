// RunExtras - what the games list's Custom Parameters and Run Game (Extra) items add to MAME's
// command line, and the files they keep (shared with the original EmuLoader):
//
// Custom parameters (CustomParam_ReadFile, CustomParam_ReadParams, WriteCustomCommandLine):
//   <data>\arcade\custom_params\mame\<name>.txt holds one line, "custom_params=<parameters>".
//   When a game runs, its own file is used, else its parent's (MAME only, as the original).
//
// Memory cards (Run Game (Extra) > Insert Memory Card):
//   <data>\arcade\games\mame_memcard_support.ini, written with the games list: a section per
//   machine with memory card slots (all Neo-Geo games share [neogeo]); the first line holds the
//   file extensions ("neo", "mc;mc2"), the next ones MAME's option for each slot ("memcard1").
//   <data>\arcade\memcard_lastused\mame\<section>.ini: [game] slot1=<file> .. slot4=<file>.
#pragma once

#include <QString>
#include <QStringList>

namespace eld {

namespace CustomParams {
QString folder(const QString& dataDir, const QString& systemBase); // <data>/arcade/custom_params/<mame>/
QString read(const QString& file);                                  // "" when there's none
bool write(const QString& file, const QString& params);             // creates the folder
// The file a game runs with: <game>.txt, else <parent>.txt; "" when neither is there.
QString fileFor(const QString& folder, const QString& game, const QString& parent);
// MAME's arguments: the game, then the parameters. A <game> (or <gamename>) tag in them is
// replaced by the game name, which then isn't put first.
QStringList arguments(const QString& game, const QString& params);
} // namespace CustomParams

struct MemoryCardSlots {
    QString section;        // the machine, or "neogeo"
    QStringList extensions; // "neo"
    QStringList options;      // MAME options: "memcard", "memcard1", "memcard2"
};

namespace MemoryCards {
QString supportFile(const QString& gamesFolder, const QString& systemBase); // <games>/mame_memcard_support.ini
QString lastUsedFile(const QString& dataDir, const QString& systemBase, const QString& section);
// The game's slots: its own section, or [neogeo] for Neo-Geo games. Empty when there are none.
MemoryCardSlots read(const QString& supportFile, const QString& game, bool neoGeo);
bool write(const QString& supportFile, const QList<MemoryCardSlots>& machines);
// IsNeoGeoDriver: the neogeo BIOS, or the neopcb driver.
bool isNeoGeo(const QString& bios, const QString& driver);
} // namespace MemoryCards

} // namespace eld
