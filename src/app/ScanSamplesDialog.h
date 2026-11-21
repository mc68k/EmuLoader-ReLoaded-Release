// ScanSamplesDialog - Menu Options > Games List > Scan MAME/HBMAME Missing Audio Samples
// (TFormArcadeScanAudioSamples): the available games that play audio samples whose samples set
// (<set>.zip or .7z in a folder of MAME's samplepath) isn't there.
//
// The samples set of each machine comes from arcade\games\mame.elsamples ("name=samples", written
// with the games list, as the original's). The list's menu: Play (Enter), Remove Selected (Del),
// Clear List, Rebuild List, Show All Games / Available Games Only / Missing ROMs/CHDs Only, and
// Save List To File.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <QHash>
#include <functional>
#include <memory>

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;
namespace Ui { class FormArcadeScanAudioSamples; }

namespace eld {

class GamesModel;

class ScanSamplesDialog : public QDialog {
    Q_OBJECT
public:
    struct Input {
        const Frontend* fe = nullptr;
        GamesModel* model = nullptr;   // the games (titles, icons, driver status)
        SystemId system = SystemId::MAME;
        std::function<void(const QString& name)> play; // selects the game in the main list and runs it
    };
    explicit ScanSamplesDialog(const Input& in, QWidget* parent = nullptr);
    ~ScanSamplesDialog() override;

    // Makes the list (AddGamesToList). False, after saying why, when there's nothing to show:
    // no samples sets list, no samples files at all, or no samples missing.
    bool scan(bool quiet = false);
    QString lastMessage() const { return message_; }
    int missingCount() const;          // the games listed (shown)
    QStringList listedNames() const;   // their set names, as listed
    bool saveList(const QString& file) const; // Save List To File

    enum Show { ShowAll, ShowHave, ShowMiss };
    void setShow(Show s);

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void filter();
    void updateTotal();
    void playSelected();
    void removeSelected();
    void saveListAsk();
    QString samplesFoldersText() const;

    std::unique_ptr<Ui::FormArcadeScanAudioSamples> ui_;
    Input in_;
    QTreeWidget* list_;
    QMenu* menu_;
    Show show_ = ShowAll;
    QString message_;
    QStringList folders_;
};

} // namespace eld
