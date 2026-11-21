// GameDocs - the MAME game documents (TFormMain.ToggleGameDocuments / ShowGameInfoAll): the
// texts of mameinfo.dat, messinfo.dat, history.xml (or history.dat), story.dat, scores3.htm
// (or marp.dat) and gameinit.dat for a machine.
//
// The files are looked for in the folders of ui.ini's "historypath" (default "dats", relative
// to MAME's folder), as the original does. Each file is kept as read, with an index of its
// entries, so finding a machine's text is a lookup; lines are decoded when they're shown
// (UTF-8, or Latin-1 for older files that aren't UTF-8).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace eld {

class GameDocs {
public:
    // The documents, numbered as in the original ([GameDocuments] DocIndex1..6 in el_extras.ini).
    enum Doc { Info = 1, History = 2, Driver = 3, Story = 4, Marp = 5, GameInit = 6 };
    static constexpr int Count = 6;

    // The folders to look in: ui.ini's historypath (ui.ini next to EmuLoader, else next to
    // MAME), relative paths against MAME's folder; MAME's folder when there's no ui.ini.
    static QStringList datFolders(const QString& dataDir, const QString& mameFolder);

    // el_extras.ini [GameDocuments] DocIndex1..6 ("<doc><0/1>"): the documents in the user's
    // order, each on or off; all six in order when the entries are missing or broken.
    static QList<QPair<int, bool>> readOrder(const class WinIni& ini);

    // Reads every file found; returns how many were found.
    int load(const QStringList& folders);
    void clear();
    bool anyLoaded() const;
    // The files read, for the status bar / log.
    QStringList loadedFiles() const { return files_; }

    struct Section {
        int doc = 0;          // Doc
        QString version;      // the file's version line, when the original shows it
        QStringList lines;    // the text, a line per line
    };
    // The documents for a machine in the order given (doc numbers, only the enabled ones).
    // name, cloneOf: the set and its parent; driver: its source file ("pacman.cpp").
    QList<Section> sections(const QString& name, const QString& cloneOf, const QString& driver,
                            const QList<int>& order) const;

private:
    struct Dat {
        QByteArray data;
        QVector<int> starts;          // where each line starts
        QHash<QString, int> index;    // "$info=" name (lower case) -> line
        bool loaded() const { return !starts.isEmpty(); }
        int count() const { return starts.size(); }
        QString line(int i) const;    // decoded, without the line break
        QByteArray raw(int i) const;
    };
    static bool read(Dat& d, const QString& file);
    static void indexInfo(Dat& d);    // "$info=a,b," lines
    QString find(const QStringList& folders, const QString& file) const;

    // "$info=<name>" block: the lines after the start tag (one of tags) up to "$end".
    static bool block(const Dat& d, const QString& name, const QStringList& tags, QStringList* out, bool* startSeen = nullptr);
    static QString version(const Dat& d, int chop);

    bool info(const QString& name, const QString& cloneOf, Section& s) const;
    bool history(const QString& name, Section& s) const;
    bool driver(const QString& driver, Section& s) const;
    bool story(const QString& name, Section& s) const;
    bool marp(const QString& name, Section& s) const;
    bool gameInit(const QString& name, const QString& cloneOf, Section& s) const;

    Dat mameInfo_, messInfo_, history_, story_, marp_, gameInit_;
    bool historyXml_ = false, marpHtml_ = false;
    int marpScoreEnd_ = 0;                  // scores3.htm: where the score column ends
    QStringList files_;
};

// XML / HTML entities: &amp; &lt; &gt; &quot; &apos; &nbsp; &#123; &#x7B; (TFormMain.DecodeHTML)
QString decodeHtmlEntities(const QString& s);

} // namespace eld
