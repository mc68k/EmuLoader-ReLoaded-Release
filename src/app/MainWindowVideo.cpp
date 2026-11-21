// Video previews: the selected game's video, played by EmuLoader's own player in the snapshot's
// picture panel (VideoPanel), or by the media player of Settings > Video > Advanced
// (TFormMain.VideoPreview*: the original's only way).
#include "MainWindow.h"

#include "DiagnosticLog.h"
#include "FileProblem.h"
#include "SettingsDialog.h"
#include "TextFile.h"
#include "ToolBarIcons.h"
#include "VideoPreviewSettingsDialog.h"
#include "ui_FormMain.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

namespace eld {

// ReadVideoPreviewIni + SetVideoPreviewState
void MainWindow::loadVideoPreview() {
    stopVideo();
    video_ = VideoPreviewSettings::load(fe_.iniFilesFolder() + "videopreview.ini");
    if (!videoTimer_) {
        // AutoPlay: once the selection has settled, so that going down the list doesn't start
        // every game's video
        videoTimer_ = new QTimer(this);
        videoTimer_->setSingleShot(true);
        videoTimer_->setInterval(200);
        connect(videoTimer_, &QTimer::timeout, this, [this] {
            const MameGame* g = current();
            if (g && g->key() == videoGame_ && !videoFile_.isEmpty()) {
                // the video waiting on its first frame plays; else (a layout change since) from the start
                if (videoPanel_ && videoPanel_->paused() && videoPanel_->file() == videoFile_ &&
                    videoPanel_->parentWidget() == videoSlotView()) {
                    videoPanel_->resume();
                    return;
                }
                videoByUser_ = false;
                playVideoPreview();
            }
        });
    }
    ui_->ButtonVideoPreviewPlay->setVisible(video_.enabled);
    if (QAction* a = menus_.action("PopupVideoPreviewPlay")) a->setVisible(video_.enabled);
    layoutImageToolBar();
    videoPlaying_ = false;
    videoGame_.clear();
    findVideoPreview(false);
    updateCategoryButton(); // Video, or the snapshot when videos can't play in the panels any more
    updateImage();          // (Video panels: "No Video", or the snapshot)
    videoPanelsChanged();
}

// The built-in player plays when it's chosen (or no other player is set) and its library is there.
bool MainWindow::internalVideo() const {
    return (video_.internal || video_.player.isEmpty()) && VideoPanel::available();
}

bool MainWindow::canPlayVideo() const { return internalVideo() || !video_.player.isEmpty(); }

bool MainWindow::videoInPanels() const { return video_.enabled && internalVideo(); }

bool MainWindow::slotVideo(int slot) const {
    if (!videoInPanels() || !layouts_) return false;
    if (layouts_->layout() == 0) return slot == 1 && imageVideo_;
    const MameGame* g = current();
    const bool cc = useConsComp_ && g && g->systemType == 1; // as slotCategory
    return slot >= 1 && slot <= 4 && (cc ? layoutSettings_.videoCC : layoutSettings_.video)[slot - 1];
}

ImageView* MainWindow::videoSlotView() const {
    for (int i = 1; i <= 4; ++i)
        if (ImageView* v = layouts_ ? layouts_->view(i) : nullptr; v && v->isVisible() && slotVideo(i)) return v;
    return nullptr;
}

// After a category or layout change: a Video panel plays the game's video (once the wheel has
// settled on it), any other stops it
void MainWindow::videoPanelsChanged() {
    if (!videoTimer_) return; // (setting up)
    if (videoSlotView() && !videoFile_.isEmpty()) {
        if (!(videoPanel_ && videoPanel_->playing() && videoPanel_->parentWidget() == videoSlotView())) {
            cueVideo();
            videoTimer_->start();
        }
    } else {
        videoTimer_->stop();
    }
}

// "No Video", drawn like the pictures' "not found" (the video's icon, why, a click to Settings > Video)
void MainWindow::showNoVideo(ImageView* v) {
    v->setProperty("imageGen", v->property("imageGen").toInt() + 1); // (a picture still loading isn't shown)
    v->setProperty("imageFile", QString());
    v->setProperty("imageZip", QString());
    v->setProperty("imageCategory", 1);
    v->setProperty("noVideo", true);
    v->setProperty("videoWaiting", false);
    v->setBackground(Qt::black);
    // as the pictures: a click to Settings > Video only when the folder is what's wrong (none set,
    // or it isn't there), else where it was looked for
    const QString setting = video_.folders.value(Frontend::fileBase(sys_)).trimmed();
    const QString folder = setting.isEmpty() ? QString() : resolvePath(fe_.dataDir(), setting);
    bool fix = false;
    QString reason;
    if (!folder.isEmpty() && !QFileInfo(folder).isDir()) {
        fix = true;
        reason = tr("The videos folder isn't there:") + '\n' + QDir::toNativeSeparators(folder) + '\n' +
                 tr("Click here to set it in Settings > Video.");
    } else {
        QStringList places;
        auto add = [&](const QString& dir) {
            const QString name = tr("the %1 folder").arg(QDir(dir).dirName());
            if (QFileInfo(dir).isDir() && !places.contains(name)) places << name;
        };
        if (!folder.isEmpty()) {
            add(folder);
            add(QDir(folder).filePath("videosnaps"));
        }
        for (const QString& snap : imageFinder_.folders(1)) add(QDir(snap).filePath("videosnaps"));
        if (!fe_.mameFolder(sys_).isEmpty()) add(QDir(fe_.mameFolder(sys_)).filePath("videosnaps"));
        if (places.isEmpty()) {
            fix = true;
            reason = tr("No videos folder is set.") + '\n' + tr("Click here to set one in Settings > Video.");
        } else {
            reason = places.size() == 1 ? tr("Not in %1.").arg(places[0])
                     : places.size() == 2 ? tr("Not in %1.").arg(tr("%1 or %2").arg(places[0], places[1]))
                                          : tr("Not in %1.").arg(tr("%1, %2 or %3").arg(places[0], places[1], places[2]));
        }
    }
    const QIcon picture = ToolBarIcons::make(fe_.mainIcon("video_preview.ico"), {}, QSize(220, 188), true);
    v->setNotFound(picture, tr("No Video"), reason, fix);
    if (!v->property("notFoundConnected").toBool()) {
        v->setProperty("notFoundConnected", true);
        connect(v, &ImageView::notFoundClicked, this,
                [this, v] { openSettings(v->property("noVideo").toBool() ? "Video" : "Pictures"); });
    }
    v->setHintText(toolIcon("video_preview.ico", false), tr("Video"), tr("not found"), false);
}

// A Video panel while its video is on the way: the panel's background only (the snapshot would
// show twice, in a layout with a Game Snapshot panel too). A click plays the video.
void MainWindow::showVideoWaiting(ImageView* v) {
    v->setProperty("imageGen", v->property("imageGen").toInt() + 1); // (a picture still loading isn't shown)
    v->setProperty("imageFile", QString());
    v->setProperty("imageZip", QString());
    v->setProperty("imageCategory", 1);
    v->setProperty("noVideo", false);
    v->setProperty("videoWaiting", true);
    v->setBackground(Qt::black);
    v->setImage(QImage(), true);
    if (!v->property("waitingConnected").toBool()) {
        v->setProperty("waitingConnected", true);
        connect(v, &ImageView::leftClicked, this, [this, v] {
            if (v->property("videoWaiting").toBool() && !(videoPanel_ && videoPanel_->isVisible())) toggleVideoPreview();
        });
    }
    v->setHintText(toolIcon("video_preview.ico", false), tr("Video"), QFileInfo(videoFile_).fileName(), false);
}

// The selected game's video in the Video panel, paused on its first frame until the selection
// settles (videoTimer_), so the panel shows a frame of it at once
void MainWindow::cueVideo() {
    if (videoFile_.isEmpty() || !internalVideo() || !videoSlotView()) return;
    videoByUser_ = false;
    playVideoPreview(false, true);
}

// GamesListViewItemSelectionChanged: the video of the selected game, the buttons enabled when it
// has one, the dummy video when it hasn't (and one is playing), AutoPlay.
void MainWindow::findVideoPreview(bool autoPlay) {
    const MameGame* g = current();
    bool newGame = g && g->key() != videoGame_;
    if (!g || newGame) stopVideo(); // another game: its picture
    videoGame_ = g ? g->key() : QString();
    videoFile_.clear();
    const bool player = canPlayVideo();
    if (g && video_.enabled && player) {
        QString folder = video_.folders.value(Frontend::fileBase(sys_)).trimmed();
        QString parent = video_.parentVideo ? g->imageParent() : QString();
        videoFile_ = VideoPreview::find(folder.isEmpty() ? QString() : resolvePath(fe_.dataDir(), folder),
                                        imageFinder_.folders(1), fe_.mameFolder(sys_), g->imageName(), parent);
    }
    bool has = !videoFile_.isEmpty();
    const QString tip = !player ? tr("Videos can't be played: EmuLoader's player (%1) isn't next to EmuLoader, and no other "
                                     "media player is set in Settings > Video.").arg(VideoPanel::libraryName())
                        : has   ? tr("Play a video of the selected game (Shift+V)")
                                : tr("The selected game has no video");
    ui_->ButtonVideoPreviewPlay->setEnabled(has);
    ui_->ButtonVideoPreviewPlay->setToolTip(tip);
    if (QAction* a = menus_.action("PopupVideoPreviewPlay")) a->setEnabled(has);
    if (!video_.enabled || !autoPlay || !newGame) return;
    playDummyVideo(false);
    if (!has) return;
    // a Video panel plays it once the selection settles; Auto Play: the other media player plays it
    if (internalVideo()) {
        if (videoSlotView()) {
            cueVideo();
            videoTimer_->start();
        }
    } else if (video_.autoPlay) {
        playVideoPreview();
    }
}

// Shift+V, the Play Video Preview button and menu item: the video, or, while it plays in the
// panel, the picture again
void MainWindow::toggleVideoPreview() {
    if (!video_.enabled) return;
    if (videoPanel_ && videoPanel_->playing()) {
        stopVideo();
        return;
    }
    videoByUser_ = true;
    playDummyVideo(false);
    playVideoPreview();
}

// The panel the video plays in: a Video panel, else the one showing the Game Snapshot, else the
// first one shown.
ImageView* MainWindow::videoView() {
    if (ImageView* v = videoSlotView()) return v;
    ImageView* first = nullptr;
    for (int i = 1; i <= 4; ++i) {
        ImageView* v = layouts_ ? layouts_->view(i) : nullptr;
        if (!v || !v->isVisible()) continue;
        if (slotCategory(i) == 1) return v;
        if (!first) first = v;
    }
    return first;
}

void MainWindow::stopVideo() {
    if (videoTimer_) videoTimer_->stop();
    if (videoPanel_) videoPanel_->stop();
}

// VideoPreviewPlay: in the panel, or "<player>" <parameters>, %1 = "<video>"; the dummy video
// (novideo.avi) with the dummy parameters (else the player's), where %1 is optional.
void MainWindow::playVideoPreview(bool dummy, bool paused) {
    if (!dummy && internalVideo()) {
        if (videoFile_.isEmpty()) return;
        ImageView* view = paused ? videoSlotView() : videoView();
        if (!view) {
            if (paused) return; // (no Video panel shown)
            // no picture panel shown (the pictures are hidden): in its own window
            if (videoByUser_) openVideoElsewhere();
            return;
        }
        if (videoPanel_ && videoPanel_->parentWidget() != view) delete videoPanel_; // the layout changed
        if (!videoPanel_) {
            videoPanel_ = new VideoPanel(view);
            connect(videoPanel_, &VideoPanel::contextMenuWanted, this, &MainWindow::showVideoMenu);
            connect(videoPanel_, &VideoPanel::failed, this, [this](const QString& why) {
                if (!videoByUser_) return; // AutoPlay: in the log only
                const QString file = videoPanel_ ? videoPanel_->file() : videoFile_;
                QMessageBox box(QMessageBox::Warning, tr("Play Video Preview"),
                                tr("%1 couldn't be played.").arg(QFileInfo(file).fileName()), QMessageBox::Ok, this);
                box.setObjectName("VideoFailedMessage");
                box.setInformativeText((why.isEmpty() ? QString() : tr("The reason given: %1").arg(why) + "\n\n") +
                                       tr("The file may be damaged, or in a format that can't be played.") + "\n\n" +
                                       QDir::toNativeSeparators(file));
                box.exec();
            });
        }
        if (videoPanel_->playing() && videoPanel_->file() == videoFile_ && !paused) { // (cued: it goes on)
            videoPanel_->resume();
            return;
        }
        videoPanel_->play(videoFile_, {video_.sound, video_.loop, video_.volume, paused});
        return;
    }
    QString file = videoFile_, params = video_.parameters;
    if (dummy) {
        file = fe_.resourcesFolder() + "images/novideo.avi";
        if (!video_.dummyParameters.isEmpty()) params = video_.dummyParameters;
        if (!QFileInfo::exists(file)) {
            videoPlaying_ = false;
            return;
        }
    }
    if (file.isEmpty() || video_.player.isEmpty()) return;
    QString player = resolvePath(fe_.dataDir(), video_.player);
    QStringList args = VideoPreview::arguments(params, QDir::toNativeSeparators(file));
    if (!QProcess::startDetached(player, args, QFileInfo(player).absolutePath())) {
        if (!dummy) FileProblem::cantStart(this, tr("Play Video Preview"), player, QString());
        return;
    }
    if (video_.playDummy) videoPlaying_ = !dummy;
}

// VideoPreviewPlayDummy: stops the playing video (Play Dummy Video to Stop Current Playback) when
// the selected game has no video, or always (force: running a game, closing EmuLoader).
void MainWindow::playDummyVideo(bool force) {
    if (!video_.playDummy || !videoPlaying_ || internalVideo()) return;
    if (videoFile_.isEmpty() || force) playVideoPreview(true);
}

// The playing video's right-click menu
void MainWindow::showVideoMenu(const QPoint& global) {
    QMenu m(this);
    m.setObjectName("VideoMenu");
    QAction* stop = m.addAction(tr("Stop the Video"));
    stop->setObjectName("VideoMenuStop");
    QAction* elsewhere = m.addAction(video_.player.isEmpty() ? tr("Open in Your Media Player")
                                                             : tr("Open in %1").arg(QFileInfo(video_.player).completeBaseName()));
    elsewhere->setObjectName("VideoMenuOpen");
    m.addSeparator();
    QAction* settings = m.addAction(tr("Video Settings..."));
    settings->setObjectName("VideoMenuSettings");
    QAction* chosen = m.exec(global);
    if (chosen == stop) stopVideo();
    else if (chosen == elsewhere) openVideoElsewhere();
    else if (chosen == settings) openSettings("Video");
}

// In the media player of Settings > Video > Advanced, else in the player the system opens videos with
void MainWindow::openVideoElsewhere() {
    const QString file = videoPanel_ && !videoPanel_->file().isEmpty() ? videoPanel_->file() : videoFile_;
    stopVideo();
    if (file.isEmpty()) return;
    if (!video_.player.isEmpty()) {
        const QString player = resolvePath(fe_.dataDir(), video_.player);
        DiagnosticLog::write("video", "open in " + QDir::toNativeSeparators(player) + ": " + QDir::toNativeSeparators(file));
        if (!QProcess::startDetached(player, VideoPreview::arguments(video_.parameters, QDir::toNativeSeparators(file)),
                                     QFileInfo(player).absolutePath()))
            FileProblem::cantStart(this, tr("Play Video Preview"), player, QString());
        return;
    }
    DiagnosticLog::write("video", "open with the system's player: " + QDir::toNativeSeparators(file));
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))
        QMessageBox::warning(this, tr("Play Video Preview"),
                             tr("No application opens %1 files. Choose one in Windows for this file type, or set a media "
                                "player in Settings > Video > Advanced.\n\n%2")
                                 .arg("." + QFileInfo(file).suffix(), QDir::toNativeSeparators(file)));
}

void MainWindow::addVideoPage(SettingsDialog& s, QVector<std::function<void()>>& after) {
    auto* dlg = new VideoPreviewSettingsDialog(fe_, sys_, video_, &s);
    dlg->settingsLayout();
    s.addDialog(tr("Video"), dlg, QString(), QString());
    after << [this, dlg] {
        if (dlg->result() != QDialog::Accepted) return;
        QString file = fe_.iniFilesFolder() + "videopreview.ini";
        if (!dlg->values().save(file))
            FileProblem::cantSave(this, tr("Video Preview Settings"), file);
        loadVideoPreview();
    };
}

} // namespace eld
