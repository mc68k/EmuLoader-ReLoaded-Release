// GameInfoPage - Internet Game Info without the internet: a web page of a game made from what
// EmuLoader already has (the games list, the game documents, the pictures), opened in the
// default browser. EmuLoaderRL\gameinfo\<name>.html (<list>\<name>.html for a software list
// game); a page is made again each time it's asked for, so it's never out of date. Pictures
// are linked where they are; the ones inside zips are copied out to gameinfo\images\.
#pragma once

#include "ElDatabase.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class GameInfoPage {
    Q_DECLARE_TR_FUNCTIONS(GameInfoPage)
public:
    struct Picture {
        QString title;   // "Title Snapshot"
        QString file;    // the picture, absolute
        bool parents = false; // the parent's (the game has none of its own)
    };
    struct Doc {
        QString title;   // "History Data"
        QString version; // the file's version line
        QStringList lines;
    };
    struct Input {
        MameGame game;
        QString parentTitle;            // the parent's title, when it's a clone
        QVector<MameGame> clones;       // its clones (a parent)
        QString system;                 // "MAME", "HBMAME"
        QString emulator;               // MAME's version
        QVector<Picture> pictures;
        QVector<Doc> docs;
        QString lastPlayed, playtime;   // as the games list shows them ("" for never)
        QString generator;              // "EmuLoader ReLoaded 1.0.0 (build 6a21352)"
    };
    // The page
    static QString html(const Input& in);
    // Where a game's page goes: <folder>\<name>.html, <folder>\<list>\<name>.html
    static QString file(const QString& folder, const MameGame& g);
    // Writes the page (UTF-8); false if it can't be written
    static bool write(const QString& file, const Input& in);
};

} // namespace eld
