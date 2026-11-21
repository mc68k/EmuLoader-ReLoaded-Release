// SplashScreen - the splash screen window (TFormStatus): at startup, while the program loads,
// with the effect chosen in Customize Splash Screen; and for long jobs (ProgressWindow).
//
// At startup the main window is built while the splash screen is up. The games list is read on
// a second thread meanwhile (runWhileShowing), so that the effect keeps playing; finish() then
// waits for the effect (and INSERT COIN) to end before the main window shows.
#pragma once

#include "SplashSettings.h"

#include <QWidget>
#include <functional>

namespace eld {

class Frontend;
class SplashView;

// The frameless window with the splash screen picture in it.
class SplashWindow : public QWidget {
    Q_OBJECT
public:
    SplashWindow(const QString& imagesFolder, const SplashSettings& s, QWidget* parent = nullptr);
    SplashView* view() const { return view_; }
    void centerOn(QWidget* w); // on that window's screen, or the primary screen

private:
    SplashView* view_;
};

namespace StartupSplash {
QString settingsFile(const Frontend& fe);   // ini_files\splash.ini
QString imagesFolder(const Frontend& fe);   // resources\images
void show(Frontend& fe);                    // plays the effect (Random: one not played last time)
bool active();
void status(const QString& title, const QString& message);
void pump();                                // draws a frame now, if one is due
// Runs work on another thread while the splash screen keeps drawing (straight away when there's none).
void runWhileShowing(const std::function<void()>& work);
// waits for the effect, then shows the main window (showMain) and closes the splash screen: in that
// order, so the main window takes over the splash screen's place in front (closed first, Windows
// would bring the window behind it, File Explorer's, to the front, and the main window open behind)
void finish(const std::function<void()>& showMain = {});
}

} // namespace eld
