#include "VideoPreviewSettingsDialog.h"

#include "ElWidgets.h"
#include "SettingsLayout.h"
#include "TextFile.h"
#include "VclRuntime.h"
#include "VideoPanel.h"
#include "ui_FormVideoPreviewSettings.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QSlider>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>

namespace eld {

namespace {
QString keyOf(SystemId s) { return s == SystemId::HBMAME ? QStringLiteral("hbmame") : QStringLiteral("mame"); }
} // namespace

VideoPreviewSettingsDialog::VideoPreviewSettingsDialog(Frontend& fe, SystemId current, const VideoPreviewSettings& s,
                                                       QWidget* parent)
    : QDialog(parent), ui_(new Ui::FormVideoPreviewSettings), fe_(fe), s_(s), folders_(s.folders) {
    ui_->setupUi(this);
    vcl::apply(this);
    setWindowTitle(tr("Video Preview Settings"));

    // Systems (MAME, HBMAME): a folder each; the rest is the same for all.
    systemsView_ = new QListWidget(this);
    systemsView_->setViewMode(QListView::IconMode);
    systemsView_->setIconSize(QSize(48, 48));
    systemsView_->setGridSize(QSize(110, 82));
    systemsView_->setMovement(QListView::Static);
    systemsView_->setFlow(QListView::LeftToRight);
    systemsView_->setFrameShape(QFrame::NoFrame);
    systemsView_->setFont(QFont("Verdana", 8));
    int start = 0;
    for (SystemId id : {SystemId::MAME, SystemId::HBMAME}) {
        QString res = fe_.mainIcon(id == SystemId::HBMAME ? "emu_hbmame.ico" : "emu_mame.ico");
        auto* it = new QListWidgetItem(QIcon(res), Frontend::section(id), systemsView_);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        bool available = !fe_.emulatorFile(id).isEmpty();
        if (!available) it->setForeground(Qt::gray); // ghosted: no emulator set
        it->setData(Qt::UserRole, available);
        if (id == current) start = int(systems_.size());
        systems_ << id;
    }
    ui_->VideoPreviewFolder->setToolTip(tr("A relative folder is relative to EmuLoader's folder. Network paths are not supported."));
    ui_->VideoPreviewMediaPlayerParameters->setToolTip(tr("%1 is replaced by the video file"));
    ui_->ButtonResetVideoPreviewFolder->setText(tr("Reset"));
    ui_->ButtonResetVideoPreviewFolder->setToolTip(tr("Reset folder to current"));
    ui_->ButtonClearVideoPreviewMediaPlayer->setText(tr("Clear"));
    ui_->ButtonClearVideoPreviewMediaPlayer->setToolTip(tr("No other media player: EmuLoader's player plays the videos"));
    for (QPushButton* b : findChildren<QPushButton*>()) b->setAutoDefault(false);

    // Values
    ui_->VideoPreviewMediaPlayerExecutable->setText(QDir::toNativeSeparators(s_.player));
    ui_->VideoPreviewMediaPlayerParameters->setText(s_.player.isEmpty() ? QString() : s_.parameters);
    ui_->VideoPreviewDummyVideoParameters->setText(s_.dummyParameters);
    ui_->VideoPreviewEnabled->setChecked(s_.enabled);
    ui_->VideoPreviewAutoPlay->setChecked(s_.autoPlay);
    ui_->VideoPreviewParentGameVideo->setChecked(s_.parentVideo);
    ui_->VideoPreviewPlayDummyVideo->setChecked(s_.playDummy);

    // Folder (per system)
    connect(ui_->VideoPreviewFolder, &QLineEdit::textChanged, this, [this](const QString& t) {
        int r = systemsView_->currentRow();
        if (!filling_ && r >= 0) folders_[keyOf(systems_[r])] = t;
    });
    connect(ui_->ButtonSelectVideoPreviewFolder, &QPushButton::clicked, this, [this] {
        int r = systemsView_->currentRow();
        if (r < 0) return;
        QString base = fe_.dataDir();
        QString now = ui_->VideoPreviewFolder->text().trimmed();
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select a folder for video preview [%1]").arg(Frontend::section(systems_[r])),
                                                        now.isEmpty() ? base : resolvePath(base, now));
        if (dir.isEmpty()) return;
        QString rel = QDir(base).relativeFilePath(dir); // relative to EmuLoader's folder when it's inside it
        ui_->VideoPreviewFolder->setText(QDir::toNativeSeparators(rel.startsWith("..") || QDir::isAbsolutePath(rel) ? dir : rel));
    });
    connect(ui_->ButtonResetVideoPreviewFolder, &QPushButton::clicked, this, [this] {
        int r = systemsView_->currentRow();
        if (r >= 0) ui_->VideoPreviewFolder->setText(s_.folders.value(keyOf(systems_[r])));
    });

    // Media player
    const QString iniFile = fe_.iniFilesFolder() + "videopreview.ini";
    connect(ui_->ButtonSelectVideoPreviewMediaPlayer, &QPushButton::clicked, this, [this, iniFile] {
        QString now = ui_->VideoPreviewMediaPlayerExecutable->text().trimmed();
        QString f = QFileDialog::getOpenFileName(this, tr("Select a media player executable"),
                                                 now.isEmpty() ? QString() : resolvePath(fe_.dataDir(), now),
#ifdef Q_OS_WIN
                                                 tr("Programs (*.exe *.bat *.cmd)"));
#else
                                                 QString());
#endif
        if (f.isEmpty()) return;
        ui_->VideoPreviewMediaPlayerExecutable->setText(QDir::toNativeSeparators(f));
        // ReadVideoPreviewIni: the player's parameters from [Parameters], if it has some
        ui_->VideoPreviewMediaPlayerParameters->setText(VideoPreviewSettings::parametersFor(iniFile, f));
    });
    // Clear: no other media player (and so none to play the videos in)
    connect(ui_->ButtonClearVideoPreviewMediaPlayer, &QPushButton::clicked, this, [this] {
        ui_->VideoPreviewMediaPlayerExecutable->clear();
        ui_->VideoPreviewMediaPlayerParameters->clear();
        if (other_) other_->setChecked(false);
    });
    connect(ui_->ButtonResetVideoPreviewMediaPlayerParameters, &QPushButton::clicked, this,
            [this] { ui_->VideoPreviewMediaPlayerParameters->setText(s_.parameters); });
    connect(ui_->ButtonResetVideoPreviewDummyVideoParameters, &QPushButton::clicked, this,
            [this] { ui_->VideoPreviewDummyVideoParameters->setText(s_.dummyParameters); });

    connect(systemsView_, &QListWidget::currentRowChanged, this, [this] { showSystem(); });
    systemsView_->setCurrentRow(start);
    showSystem();
    systemsView_->setFocus();
}

VideoPreviewSettingsDialog::~VideoPreviewSettingsDialog() = default;

// SystemsVideoPreviewItemSelectionChanged
void VideoPreviewSettingsDialog::showSystem() {
    int r = systemsView_->currentRow();
    if (r < 0) return;
    bool available = systemsView_->item(r)->data(Qt::UserRole).toBool();
    if (notAvailable_) {
        notAvailable_->setText(available ? QString() : tr("No emulator is set for %1.").arg(Frontend::section(systems_[r])));
        notAvailable_->setVisible(!available);
    }
    filling_ = true;
    ui_->VideoPreviewFolder->setText(folders_.value(keyOf(systems_[r])));
    filling_ = false;
}

// The Settings window's Video tab: where the videos are (a folder per system) and the media player
// that plays them, in groups; the ? buttons' texts under what they're about.
void VideoPreviewSettingsDialog::settingsLayout() {
    using settingsui::Group;
    QLabel* like = new QLabel(this);
    like->setStyleSheet("color: #6E6E6E;");
    like->hide();
    auto hint = [like](const QString& t) { return settingsui::hint(t, like); };
    notAvailable_ = hint(QString());
    notAvailable_->setStyleSheet("color: #B00000;");

    ui_->VideoPreviewEnabled->setText(tr("Enable Video Previews"));
    ui_->VideoPreviewParentGameVideo->setText(tr("Parent Game Video"));
    ui_->LabelVideoPreviewFolder->setText(tr("Folder"));
    Group videos(tr("Videos"));
    videos.check(ui_->VideoPreviewEnabled, hint(tr("Play a game's video with Shift+V, the Play Video Preview button above the "
                                                   "pictures, or the games list's right-click menu.")))
        .check(ui_->VideoPreviewParentGameVideo, hint(tr("A clone without a video plays its parent's.")))
        .space(4)
        .row(new QLabel(tr("System")), settingsui::systemSwitch(systemsView_))
        .row(ui_->LabelVideoPreviewFolder, ui_->VideoPreviewFolder, {ui_->ButtonSelectVideoPreviewFolder, ui_->ButtonResetVideoPreviewFolder})
        .row(nullptr, hint(tr("A folder inside EmuLoader's folder can be given as a relative path; network paths aren't "
                              "supported, and subfolders aren't searched.")))
        .row(nullptr, hint(tr("Videos that aren't in the folder (or all of them, with no folder) are also looked for in a "
                              "videosnaps folder inside the emulator's folder, in a videosnaps folder inside the snapshot "
                              "folder, and with the snapshots themselves.")))
        .row(nullptr, notAvailable_);

    // EmuLoader's own player: in the picture panel
    sound_ = new QCheckBox(tr("Sound"));
    sound_->setObjectName("VideoSound");
    sound_->setChecked(s_.sound);
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setObjectName("VideoVolume");
    volume_->setRange(0, 100);
    volume_->setSingleStep(5);
    volume_->setPageStep(10);
    volume_->setValue(s_.volume);
    volume_->setMinimumWidth(160);
    auto* volumeText = new QLabel;
    volumeText->setMinimumWidth(QFontMetrics(volumeText->font()).horizontalAdvance("100%") + 4);
    auto showVolume = [this, volumeText] {
        volumeText->setText(QString("%1%").arg(volume_->value()));
        volume_->setEnabled(sound_->isChecked());
    };
    connect(volume_, &QSlider::valueChanged, this, showVolume);
    connect(sound_, &QCheckBox::toggled, this, showVolume);
    showVolume();
    loop_ = new QCheckBox(tr("Play Again When It Ends"));
    loop_->setObjectName("VideoLoop");
    loop_->setChecked(s_.loop);
    library_ = hint(QString());
    library_->setObjectName("VideoPlayerLibrary");
    if (VideoPanel::available()) {
        library_->setText(VideoPanel::standIn() ? tr("EmuLoader's player is ready.")
                                                : tr("EmuLoader's player is ready (mpv %1, %2).")
                                                      .arg(VideoPanel::version(), QFileInfo(VideoPanel::libraryFile()).fileName()));
    } else {
        library_->setText(tr("EmuLoader's player can't play videos: %1 isn't in EmuLoader's folder. It comes with "
                             "EmuLoader; put it back from the download, or use another media player (Advanced, below).")
                              .arg(VideoPanel::libraryName()));
        library_->setStyleSheet("color: #B00000;");
    }
    auto* volumeLabel = new QLabel(tr("Volume"));
    Group player(tr("Video Player"), "VideoPlayerGroup");
    player.text(hint(tr("To watch the videos as you go through the games, turn a picture panel to Video: with the mouse "
                        "wheel or the category button (Video comes after Game Snapshot), or for a panel of a layout in "
                        "Settings > Layouts.")))
        .space(2)
        .check(sound_)
        .row(volumeLabel, volume_, {volumeText})
        .check(loop_, hint(tr("Off: the snapshot shows again when the video ends.")))
        .space(4)
        .text(hint(tr("Shift+V or the Play Video Preview button plays the video in any category, over the snapshot. Click "
                      "the video or press Shift+V to stop it, and right-click it to open it in your media player.")))
        .text(library_);

    // Advanced: another media player, in its own window (the original's only way)
    advancedButton_ = new QToolButton;
    advancedButton_->setObjectName("VideoAdvancedButton");
    advancedButton_->setCheckable(true);
    advancedButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advancedButton_->setAutoRaise(true);
    advancedButton_->setText(tr("Advanced: Use Another Media Player"));
    player.space(4).wide(advancedButton_);

    other_ = new QCheckBox(tr("Play the videos in this media player instead"));
    other_->setObjectName("VideoUseOtherPlayer");
    other_->setChecked(!s_.internal);
    auto* warning = hint(tr("This player opens the videos in its own window: they never play in EmuLoader's picture panel, "
                            "and EmuLoader can't stop or close it. Unchecked, it's only used by Open in... in the video's "
                            "right-click menu (and when EmuLoader's player can't play)."));
    warning->setStyleSheet("color: #B00000;");
    ui_->LabelVideoPreviewMediaPlayerExecutable->setText(tr("Media Player"));
    ui_->LabelVideoPreviewMediaPlayerParameters->setText(tr("Parameters"));
    ui_->LabelVideoPreviewDummyVideoParameters->setText(tr("Dummy Video Parameters"));
    Group advanced(tr("Another Media Player"), "VideoAdvancedGroup");
    ui_->VideoPreviewAutoPlay->setText(tr("Auto Play"));
    advanced.check(other_, warning)
        .row(ui_->LabelVideoPreviewMediaPlayerExecutable, ui_->VideoPreviewMediaPlayerExecutable,
             {ui_->ButtonSelectVideoPreviewMediaPlayer, ui_->ButtonClearVideoPreviewMediaPlayer})
        .row(nullptr, hint(tr("Any media player works, for example MPC-HC or VLC media player, and so does a batch file "
                              "(.bat, .cmd). The parameters of known players come from ini_files\\videopreview.ini.")))
        .row(ui_->LabelVideoPreviewMediaPlayerParameters, ui_->VideoPreviewMediaPlayerParameters,
             {ui_->ButtonResetVideoPreviewMediaPlayerParameters})
        .row(nullptr, hint(tr("%1 is replaced by the video file (in quotes). Include the player's switch to close when the "
                              "video ends.")))
        .space(4)
        .check(ui_->VideoPreviewAutoPlay,
               hint(tr("Starts this player when you select a game that has a video. Depending on the player, selecting "
                       "other games while a video plays can cause trouble: close it with its keyboard shortcut.")))
        .check(ui_->VideoPreviewPlayDummyVideo,
               hint(tr("When you select a game without a video, a very short video (resources\\images\\novideo.avi) "
                       "plays to stop the one playing, as EmuLoader can't close the player.")))
        .row(ui_->LabelVideoPreviewDummyVideoParameters, ui_->VideoPreviewDummyVideoParameters,
             {ui_->ButtonResetVideoPreviewDummyVideoParameters})
        .row(nullptr, hint(tr("Empty: the media player's parameters are used. %1 is optional here.")));
    QGroupBox* advancedBox = advanced.box();
    auto fold = [this, advancedBox](bool open) {
        advancedBox->setVisible(open);
        advancedButton_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        settingsui::refit(this);
    };
    connect(advancedButton_, &QToolButton::toggled, this, fold);
    for (QWidget* w : std::initializer_list<QWidget*>{ui_->VideoPreviewEnabled, ui_->VideoPreviewParentGameVideo,
                                                     ui_->VideoPreviewAutoPlay, ui_->VideoPreviewPlayDummyVideo}) {
        QFont f = w->font();
        f.setBold(false);
        w->setFont(f);
    }

    settingsui::Page p("VideoPage", tr("Video previews of the games: where the videos are, and how they play."), 2);
    p.add(0, videos).add(1, player).full(advanced).finish();
    settingsui::setPage(this, p.widget());
    // open when it's in use, or a player is set: once it's in the page (shown before, it would be
    // a window of its own for a moment)
    {
        QSignalBlocker b(advancedButton_);
        advancedButton_->setChecked(!s_.internal || !s_.player.isEmpty());
    }
    fold(advancedButton_->isChecked());
    showSystem();
}

VideoPreviewSettings VideoPreviewSettingsDialog::values() const {
    VideoPreviewSettings v = s_;
    v.folders = folders_;
    v.player = ui_->VideoPreviewMediaPlayerExecutable->text().trimmed();
    v.parameters = ui_->VideoPreviewMediaPlayerParameters->text().trimmed();
    v.dummyParameters = ui_->VideoPreviewDummyVideoParameters->text().trimmed();
    v.enabled = ui_->VideoPreviewEnabled->isChecked();
    v.autoPlay = ui_->VideoPreviewAutoPlay->isChecked();
    v.parentVideo = ui_->VideoPreviewParentGameVideo->isChecked();
    v.playDummy = ui_->VideoPreviewPlayDummyVideo->isChecked();
    if (sound_) { // (settingsLayout)
        v.sound = sound_->isChecked();
        v.volume = volume_->value();
        v.loop = loop_->isChecked();
        v.internal = !other_->isChecked() || v.player.isEmpty(); // (no other player: EmuLoader's)
    }
    return v;
}

} // namespace eld
