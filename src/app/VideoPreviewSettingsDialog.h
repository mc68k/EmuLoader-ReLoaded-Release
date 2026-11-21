// VideoPreviewSettingsDialog - Video Preview Settings (TFormVideoPreviewSettings,
// uVideoPreviewSettings.pas; image menu, games menu or Menu Options > Images): the media player
// that plays the games' videos, its parameters, a folder of videos per system (MAME, HBMAME), and
// Enabled, Parent Game Video, AutoPlay and Play Dummy Video. Ok saves to
// ini_files\videopreview.ini (the caller's job, see VideoPreviewSettings::save).
#pragma once

#include "Frontend.h"
#include "VideoPreview.h"

#include <QDialog>
#include <memory>

namespace Ui { class FormVideoPreviewSettings; }
class QListWidget;
class QLabel;
class QCheckBox;
class QSlider;
class QToolButton;

namespace eld {

class VideoPreviewSettingsDialog : public QDialog {
    Q_OBJECT
public:
    VideoPreviewSettingsDialog(Frontend& fe, SystemId current, const VideoPreviewSettings& s, QWidget* parent = nullptr);
    ~VideoPreviewSettingsDialog() override;

    VideoPreviewSettings values() const;
    // As the Settings window's Video tab: groups that fill the tab (SettingsLayout)
    void settingsLayout();

private:
    void showSystem();

    std::unique_ptr<Ui::FormVideoPreviewSettings> ui_;
    Frontend& fe_;
    VideoPreviewSettings s_;      // as it was (Reset)
    QHash<QString, QString> folders_;
    QListWidget* systemsView_ = nullptr;
    QList<SystemId> systems_;
    bool filling_ = false;
    QLabel* notAvailable_ = nullptr; // (settingsLayout) no emulator for the system
    // (settingsLayout) EmuLoader's own player, and Advanced: another media player
    QCheckBox* sound_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QCheckBox* other_ = nullptr;
    QSlider* volume_ = nullptr;
    QLabel* library_ = nullptr;
    QToolButton* advancedButton_ = nullptr;
};

} // namespace eld
