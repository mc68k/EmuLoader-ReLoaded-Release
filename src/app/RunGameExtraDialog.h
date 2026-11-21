// RunGameExtraDialog - Run Game (Extra) (TFormArcadeRunGameExtraMAME, uArcadeRunExtraMAME.pas;
// games list menu > Run Game (Extra)): before the game runs, pick what MAME should do besides:
//   Playback/Record an Input File   -playback / -record <file.inp> (input_directory), with
//                                   -exit_after_playback or -record_timecode
//   Load a Save State               -state <name> (state_directory\<game>\<name>.sta), -autosave
//   Insert Memory Card              -memcard1 <file> ... for the machines that have slots
//                                   (arcade\games\mame_memcard_support.ini, written with the games
//                                   list); the last files used are kept in
//                                   arcade\memcard_lastused\mame\<machine>.ini
//   Record Current Section to a Movie  -aviwrite / -mngwrite / -wavwrite, in
//                                   snapshot_directory\record_video\<game> folder
// Only the pages whose Enabled box is ticked count. The lists show the files there are; F2 (or
// the right-click menu) renames one, Delete deletes it; Enter or a double click on a memory card
// puts it in a slot.
//
// Folders and the option values come from MAME's ini files: mame.ini for the folders, the game's
// whole chain (mame.ini ... <game>.ini) for autosave, exit_after_playback and record_timecode;
// an option is only put on the command line when it differs from what MAME would use.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QIcon>
#include <memory>

namespace Ui { class FormArcadeRunGameExtraMAME; }
class QStandardItemModel;
class QTreeView;
class QLineEdit;
class QPushButton;
class QCheckBox;

namespace eld {

class RunGameExtraDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        MameGame game;
        QIcon gameIcon;
        QString emuIcon;  // main_icons\emu_mame.ico
        QString nameLine; // the status bar's game name text
    };
    RunGameExtraDialog(Frontend& fe, SystemId sys, const Input& in, QWidget* parent = nullptr);
    ~RunGameExtraDialog() override;

    QStringList arguments() const { return args_; } // what goes after the game (and its custom parameters)

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    enum Page { PageInput = 1, PageState, PageCard, PageMovie };
    struct List { QTreeView* view; QStandardItemModel* model; Page page; };

    void readMameSettings();
    void setupPages();
    void showPage(int page);
    void setEnabledPage(int page, bool on);
    void fillList(Page page);
    void addFile(Page page, const QString& folder, const QString& file);
    void addMatching(Page page, const QString& folder, const QString& pattern);
    void selected(Page page);
    void selectFile(Page page, const QString& path);
    void setInputMode(bool record);
    void filterMovies();
    QString movieExtension() const;
    void reselectInput();
    void reselectMovie();
    void renameFile(Page page);
    void deleteFile(Page page);
    void onRenamed(Page page, int row);
    void assignCard(int slot); // -1: by the file's extension (.mc2 to slot 2)
    void setupMemoryCards();
    void accept() override;
    bool buildArguments();
    void saveLastUsedCards();
    List& list(Page page);
    static QString cleanName(QString name); // no characters a file name can't have

    std::unique_ptr<Ui::FormArcadeRunGameExtraMAME> ui_;
    Frontend& fe_;
    SystemId sys_;
    Input in_;
    QList<List> lists_;
    QStringList args_;
    int page_ = 0;
    bool renaming_ = false;

    // mame.ini folders (absolute), the movie root (snapshot_directory)
    QString folderInput_, folderState_, folderCard_, folderMovie_, movieRoot_;
    // what MAME would use without any command line option
    bool autosave_ = false, exitAfterPlayback_ = false, recordTimecode_ = false, inputExtras_ = false;
    QString playbackFile_, recordFile_;            // remembered per mode (InputPlayback_CurrentFile ...)
    QString movieFile_[3];                         // AVI, MNG, WAV
    QString cardSection_;                          // memcard_support.ini section, "neogeo" for Neo-Geo
    QStringList cardSlots_, cardExtensions_;
    QString lastUsedCard_[4];
    QLineEdit* cardEdit_[4] = {};
};

} // namespace eld
