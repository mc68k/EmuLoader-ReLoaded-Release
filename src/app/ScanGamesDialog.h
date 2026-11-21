// ScanGamesDialog - the games list's menu > Scan Games (Audit ROMs)...: the whole scan in one
// window (made from the original's TFormArcadeScanGamesMode, whose scan mode was a setting the
// Scan Games submenu's items used):
//   Which Games: all, available, missing, incomplete (missing ROMs/CHDs) or the selected ones,
//   each with how many there are; the selected ones when several are selected.
//   How: Full Scan (MAME checks every ROM; with CHDs and audio samples), Quick Scan (the sets'
//   files), Mark Games Available (no checking; the original's Force Games Available); and, with
//   software list games in the list, Scan MAME Sets (all, arcade/machines, software list games).
//   Scan starts it. The choices are kept: [Preferences] ScanGamesMode (0, 1, 2) and
//   ScanSetsModeMAME, the original's keys; [EmuLoaderRL] AuditCHDs, AuditSamples.
#pragma once

#include "Frontend.h"

#include <QDialog>
#include <array>
#include <memory>

namespace Ui { class FormArcadeScanGamesMode; }
class QCheckBox;
class QRadioButton;
class QLabel;

namespace eld {

class ScanGamesDialog : public QDialog {
    Q_OBJECT
public:
    enum Which { All, Available, Missing, Incomplete, Selected };
    struct Input {
        Frontend* fe = nullptr;
        std::array<int, 5> counts{};  // how many games each choice has
        bool softwareLists = false;   // the list has software list games (Scan MAME Sets)
    };
    explicit ScanGamesDialog(const Input& in, QWidget* parent = nullptr);
    ~ScanGamesDialog() override;
    void accept() override; // keeps the choices

    Which which() const;

    // The kept choices
    static int mode(Frontend& fe);       // 0 full, 1 quick, 2 mark available
    static int setsMode(Frontend& fe);   // 0 all, 1 arcade/machines, 2 software list games
    static bool chds(Frontend& fe);
    static bool samples(Frontend& fe);

    void choose(Which which, int mode, bool chds, bool samples, int sets = -1);
    QCheckBox* chdsBox() const { return chds_; }
    bool canChoose(Which w) const;

private:
    void layOut();
    void updateEnabled();
    std::unique_ptr<Ui::FormArcadeScanGamesMode> ui_;
    Input in_;
    std::array<QRadioButton*, 5> which_{};
    QCheckBox* chds_ = nullptr;
    QCheckBox* samples_ = nullptr;
    QLabel* checksLabel_ = nullptr;
};

} // namespace eld
