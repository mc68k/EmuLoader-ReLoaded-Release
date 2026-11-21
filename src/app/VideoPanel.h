// VideoPanel - EmuLoader's own video player: a game's video played in a picture panel, over the
// picture, with mpv's library (libmpv: mpv-2.dll, libmpv-2.dll or mpv.dll next to EmuLoader, or
// installed; libmpv.so.2 on Linux). The library is loaded when it's first needed, not linked:
// without it EmuLoader works as before, and the media player of Settings > Video is used.
//
// mpv draws into this widget's own window (its "wid" option): letterboxed, the panel's background
// around it. A click stops it (stopped(): the picture shows again); a right-click asks for the
// video's menu. mpv's own window is turned off for the mouse (Windows: disabled, so its clicks go
// to this one), as mpv doesn't take the mouse (input-cursor=no).
//
// For testing, setStandIn(true) plays nothing: it records the file and shows its name, so
// what EmuLoader asks of the player can be checked without the library.
#pragma once

#include <QString>
#include <QWidget>

class QLabel;

namespace eld {

class VideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit VideoPanel(QWidget* parent);
    ~VideoPanel() override;

    // The library: whether it can be loaded, its file and mpv's version ("" when not loaded)
    static bool available();
    static QString libraryFile();
    static QString version();
    static QString loadError(); // why it couldn't be loaded
    static QString libraryName(); // the file looked for first (mpv-2.dll)
    static void setStandIn(bool on);
    static bool standIn();

    struct Options { bool sound = true, loop = true; int volume = 50; bool paused = false; };
    // Plays file over the parent (it fills it, following its size). paused: shows its first
    // frame and waits for resume() (a Video panel, while the selection settles)
    bool play(const QString& file, const Options& o);
    void resume();
    void stop();
    bool playing() const { return playing_; }
    bool paused() const { return playing_ && paused_; }
    QString file() const { return file_; }

signals:
    void stopped(); // clicked, ended (without Loop) or failed: the picture again
    void failed(const QString& why);
    void contextMenuWanted(const QPoint& globalPos);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    bool ensureMpv();
    void drainEvents();
    void command(const QStringList& args);
    void passMouseToPanel();
    static void wakeup(void* self);

    void* mpv_ = nullptr;
    bool playing_ = false;
    bool paused_ = false;
    QString file_;
    QLabel* standInLabel_ = nullptr;
};

} // namespace eld
