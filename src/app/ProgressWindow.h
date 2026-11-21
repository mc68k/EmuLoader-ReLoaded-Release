// ProgressWindow - progress of a long job (creating the games list, scanning or deleting
// files), shown as the original does it: the splash screen (TFormStatus) with the job's title,
// the current step, a progress bar, a count and the elapsed time; plus Cancel (and Esc) when the
// job can stop.
//
// Works without an event loop of its own: the job calls step() / setProgress() as it goes, and
// those keep the window drawn and answering (the elapsed time is updated on those calls).
#pragma once

#include <QDialog>
#include <QElapsedTimer>

class QPushButton;

namespace eld {

class SplashView;

class ProgressWindow : public QDialog {
    Q_OBJECT
public:
    ProgressWindow(const QString& title, QWidget* parent = nullptr);
    // A new step: its text and how many units it has (0 = unknown: no bar).
    void step(const QString& text, int total = 0);
    // Units done. With an estimated total the bar stops just short of the end until finish().
    void setProgress(int done, const QString& countText = {});
    void setCancelable(bool on);
    bool canceled() const { return canceled_; }
    void reject() override; // Esc: Cancel, when allowed

    // Where the splash screen's look comes from (set once by the main window).
    static void setLook(const QString& imagesFolder, const QString& splashIni);

private:
    void refresh(bool force = false);
    SplashView* view_;
    QPushButton* cancel_;
    QElapsedTimer clock_, lastPaint_;
    int total_ = 0;
    bool canceled_ = false;
};

} // namespace eld
