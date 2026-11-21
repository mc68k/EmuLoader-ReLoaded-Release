// VideoPreview - video previews (Video Preview Settings, Play Video Preview; TFormMain.VideoPreview*,
// uVideoPreviewSettings.pas): a game's video is played in the picture panel by EmuLoader's own
// player (VideoPanel, with mpv's library), or with a media player of your choice (MPC-HC, VLC
// ...), which EmuLoader starts with the file.
//
// Settings: ini_files\videopreview.ini, shared with the original:
//   [Arcade Folders]  mame=<folder>, hbmame=<folder>   a folder of videos per system (optional)
//   [MediaPlayer]     File, Enabled, AutoPlay, ParentGameVideo, PlayDummyVideo, DummyVideoParameters
//   [EmuLoaderRL]     InternalPlayer, Sound, Volume, Loop   (EmuLoader's own player; new)
//   [Parameters]      <player file name>=<parameters>, "%1" for the video ("mpc-hc.exe=%1 /play /close")
//
// Where a video is looked for (VideoPreviewFindFile, VideoPreviewFindFile_MAME), any extension:
//   <folder>\<name>.*, <parent>.*, then in <folder>\videosnaps\    the system's folder, when set
//   <snap>\videosnaps\<name>.*, <snap>\videosnaps\videosnaps\<name>.*   each snapshot folder
//   <MAME folder>\videosnaps\<name>.*
//   <snap>\<name>.* that isn't a picture (.png, .jpg, .gif)
//   then these three for the parent's name.
// The parent is given for a clone when Parent Game Video is on.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace eld {

struct VideoPreviewSettings {
    QString player;            // the media player (.exe, .bat, .cmd)
    QString parameters;        // "%1 /play /close"
    QString dummyParameters;   // for novideo.avi ("" = parameters)
    bool enabled = true, autoPlay = false, parentVideo = false, playDummy = false;
    // EmuLoader's own player ([EmuLoaderRL] in the same file: the original doesn't read it): the
    // video in the picture panel instead of the media player, with or without sound, again when
    // it ends
    bool internal = true, sound = true, loop = true;
    int volume = 50; // 0..100
    QHash<QString, QString> folders; // "mame" -> folder

    static VideoPreviewSettings load(const QString& file);
    bool save(const QString& file) const;
    // [Parameters] <player>: the known players' parameters (from videopreview[default].ini)
    static QString parametersFor(const QString& file, const QString& player);
};

namespace VideoPreview {
// The video of `name` (else of `parent`, when given); "" when there's none.
QString find(const QString& folder, const QStringList& snapFolders, const QString& mameFolder,
             const QString& name, const QString& parent);
// The player's arguments: the parameters with %1 replaced by the file ("%1" alone when empty).
QStringList arguments(const QString& parameters, const QString& file);
} // namespace VideoPreview

} // namespace eld
