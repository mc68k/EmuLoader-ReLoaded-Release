// MameSettingsDialog - port of EmuLoader's TFormMAMESettings (uMAMESettings.pas).
//
// The window is the converted FormMAMESettings.ui. Options are read and written
// through the table extracted from the Delphi source (MameOptionMap.inc); the
// handful of options with custom logic are handled in readSpecial/writeSpecial.
//
// Two modes, as in the original:
//   * level 0  - edit mame.ini itself (all options, including folders)
//   * level>0  - edit one file of MAME's ini chain for a machine (game.ini,
//                parent.ini, source/<driver>.ini, vertical.ini, ...). The
//                dialog shows the merged values up to that level, and saving
//                writes only the values that differ from what MAME would get
//                without that file.
#pragma once

#include "Frontend.h"
#include "MameIni.h"

#include <QDialog>
#include <QMap>
#include <memory>

namespace Ui { class FormMAMESettings; }
class QStandardItemModel;
class QStackedLayout;
class QTreeView;

namespace eld {

struct SettingEntry;

class MameSettingsDialog : public QDialog {
    Q_OBJECT
public:
    MameSettingsDialog(Frontend& fe, SystemId sys, QWidget* parent = nullptr);
    ~MameSettingsDialog() override;

    // Edit a chain file for this machine instead of mame.ini (level 1..12).
    void setCustom(const MameGame& game, int level);

    // Find a Setting: this window's settings, and opening the page of one with it highlighted.
    void addSettingsTo(QList<SettingEntry>& out);
    QMap<QString, QString> values() const { return collect(); } // every option the window shows: its value now
    void reveal(const QString& objectName);

protected:
    void accept() override;

private:
    void setupPages();
    void showPage(int tag);
    void showVideoEffects(int subTag); // Video 3: 1 BGFX, 2 Direct3D (HLSL), 3 OpenGL (GLSL)
    QStackedLayout* pages_ = nullptr;
    QWidget* bgfx_ = nullptr; // Video 3's BGFX settings (Panel4_Video3BGFX)
    void setupFolderList(const QString& prefix);
    void load();
    bool save(QString* error);
    void apply(const QMap<QString, QString>& values, bool fromMameIni);
    QMap<QString, QString> collect() const;
    bool readSpecial(const QString& key, const QString& value, bool fromMameIni);
    void writeSpecial(QMap<QString, QString>& out) const;
    QStandardItemModel* folderModel(const QString& prefix) const;

    // Options with their own logic (MameSettingsExtras.cpp)
    void setupExtras();                       // lists, screens, fonts, latency: once
    void setupButtons();                      // browse / reset / clear / help buttons
    void enableForMameIni(const MameIniFile& mameIni); // EnableSettingsIniMAME
    void populateShaderChains();
    QString relativeToMame(const QString& path) const;
    QStringList artworkFolders() const;
    QString shaderChainsAtLoad_ = "default";
    QStandardItemModel* chainsModel_ = nullptr;
    void prepareExtras(const MameIniFile& mameIni); // lists that depend on mame.ini's folders
    bool readExtra(const QString& key, const QString& value, bool fromMameIni);
    void writeExtras(QMap<QString, QString>& out) const;
    void loadUiIni();                         // skip_warnings lives in ui.ini
    bool saveUiIni(QString* error) const;
    bool savePluginIni(QString* error) const; // plugin.ini (mame.ini level only)
    void showScreen(int index);
    QString pluginIniPath() const;

    struct ScreenInfo { QString name = "auto", aspect = "auto", resolution = "auto", refresh = "auto", view = "auto"; };
    ScreenInfo screens_[5];                   // [0] all screens (screen/aspect/...), [1..4] screen0..screen3
    int screen_ = 0;
    bool loadingScreen_ = false;
    QStandardItemModel* biosModel_ = nullptr;
    QStandardItemModel* langModel_ = nullptr;
    QStandardItemModel* pluginModel_ = nullptr;
    QString debuggerFont_ = "Lucida Console";
    bool decimalLatency_ = true;
    QString languageAtLoad_;

    std::unique_ptr<Ui::FormMAMESettings> ui_;
    Frontend& fe_;
    SystemId sys_;
    int level_ = 0;
    ChainMachine machine_;
    QString iniName_;                         // the chain file edited, without .ini (apple2e)
    MameGame game_;
    bool sdlMame_ = false;
    QMap<QString, QStandardItemModel*> folders_;
};

} // namespace eld
