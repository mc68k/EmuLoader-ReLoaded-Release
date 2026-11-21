#include "VideoPanel.h"

#include "DiagnosticLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLibrary>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <QTimer>
#include <QWheelEvent>

#include <cstdint>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace eld {

namespace {

// The few parts of mpv's client API used (mpv/client.h, API 1.x and 2.x alike)
enum { MPV_EVENT_NONE = 0, MPV_EVENT_SHUTDOWN = 1, MPV_EVENT_END_FILE = 7, MPV_EVENT_FILE_LOADED = 8 };
enum { MPV_END_FILE_REASON_EOF = 0, MPV_END_FILE_REASON_ERROR = 4 };
struct mpv_event {
    int event_id;
    int error;
    uint64_t reply_userdata;
    void* data;
};
struct mpv_event_end_file {
    int reason;
    int error;
};

struct Api {
    void* (*create)() = nullptr;
    int (*initialize)(void*) = nullptr;
    void (*terminate_destroy)(void*) = nullptr;
    int (*set_option_string)(void*, const char*, const char*) = nullptr;
    int (*set_property_string)(void*, const char*, const char*) = nullptr;
    int (*command)(void*, const char**) = nullptr;
    mpv_event* (*wait_event)(void*, double) = nullptr;
    void (*set_wakeup_callback)(void*, void (*)(void*), void*) = nullptr;
    const char* (*error_string)(int) = nullptr;
    unsigned long (*client_api_version)() = nullptr;
    char* (*get_property_string)(void*, const char*) = nullptr;
    void (*free)(void*) = nullptr;
};

QLibrary* gLib = nullptr;
Api gApi;
bool gTried = false, gStandIn = false;
QString gError, gFile, gVersion;

QStringList libraryNames() {
#ifdef Q_OS_WIN
    return {"mpv-2.dll", "libmpv-2.dll", "mpv-1.dll", "mpv.dll"};
#elif defined(Q_OS_MACOS)
    return {"libmpv.2.dylib", "libmpv.dylib"};
#else
    return {"libmpv.so.2", "libmpv.so.1", "libmpv.so"};
#endif
}

bool load() {
    if (gTried) return gApi.create != nullptr;
    gTried = true;
    // next to EmuLoader first, then wherever the system finds it
    const QString here = QCoreApplication::applicationDirPath();
    const QStringList names = libraryNames();
    for (const QString& n : names)
        for (const QString& path : {QDir(here).filePath(n), n}) {
            auto* lib = new QLibrary(path);
            if (!lib->load()) {
                delete lib;
                continue;
            }
            Api a;
            a.create = reinterpret_cast<void* (*)()>(lib->resolve("mpv_create"));
            a.initialize = reinterpret_cast<int (*)(void*)>(lib->resolve("mpv_initialize"));
            a.terminate_destroy = reinterpret_cast<void (*)(void*)>(lib->resolve("mpv_terminate_destroy"));
            a.set_option_string = reinterpret_cast<int (*)(void*, const char*, const char*)>(lib->resolve("mpv_set_option_string"));
            a.set_property_string = reinterpret_cast<int (*)(void*, const char*, const char*)>(lib->resolve("mpv_set_property_string"));
            a.command = reinterpret_cast<int (*)(void*, const char**)>(lib->resolve("mpv_command"));
            a.wait_event = reinterpret_cast<mpv_event* (*)(void*, double)>(lib->resolve("mpv_wait_event"));
            a.set_wakeup_callback = reinterpret_cast<void (*)(void*, void (*)(void*), void*)>(lib->resolve("mpv_set_wakeup_callback"));
            a.error_string = reinterpret_cast<const char* (*)(int)>(lib->resolve("mpv_error_string"));
            a.client_api_version = reinterpret_cast<unsigned long (*)()>(lib->resolve("mpv_client_api_version"));
            a.get_property_string = reinterpret_cast<char* (*)(void*, const char*)>(lib->resolve("mpv_get_property_string"));
            a.free = reinterpret_cast<void (*)(void*)>(lib->resolve("mpv_free"));
            if (!a.create || !a.initialize || !a.terminate_destroy || !a.set_option_string || !a.command || !a.wait_event ||
                !a.set_wakeup_callback) {
                gError = QCoreApplication::translate("eld::VideoPanel", "%1 isn't mpv's library (libmpv).").arg(n);
                lib->unload();
                delete lib;
                return false;
            }
            gLib = lib;
            gApi = a;
            gFile = lib->fileName();
            if (a.client_api_version) {
                const unsigned long v = a.client_api_version();
                gVersion = QString("%1.%2").arg(v >> 16).arg(v & 0xffff);
            }
            DiagnosticLog::write("video", "mpv library: " + QDir::toNativeSeparators(gFile) + ", client API " + gVersion);
            return true;
        }
    gError = QCoreApplication::translate("eld::VideoPanel", "mpv's library isn't there (%1, next to EmuLoader).").arg(names.first());
    return false;
}

} // namespace

bool VideoPanel::available() { return gStandIn || load(); }
QString VideoPanel::libraryFile() { return gStandIn ? QStringLiteral("(stand-in)") : (load(), gFile); }
QString VideoPanel::version() { return gVersion; }
QString VideoPanel::libraryName() { return libraryNames().first(); }
QString VideoPanel::loadError() { return (load(), gError); }
void VideoPanel::setStandIn(bool on) { gStandIn = on; }
bool VideoPanel::standIn() { return gStandIn; }

VideoPanel::VideoPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("VideoPanel");
    // mpv draws in a window of its own: this one (not its parents)
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    if (!gStandIn) setAttribute(Qt::WA_NativeWindow);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click to stop the video"));
    if (parent) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
    hide();
}

VideoPanel::~VideoPanel() {
    if (mpv_ && gApi.terminate_destroy) {
        gApi.set_wakeup_callback(mpv_, nullptr, nullptr);
        gApi.terminate_destroy(mpv_);
    }
}

bool VideoPanel::ensureMpv() {
    if (gStandIn || mpv_) return true;
    if (!load()) return false;
    void* m = gApi.create();
    if (!m) {
        gError = tr("mpv couldn't be started.");
        return false;
    }
    // into this window, nothing of its own (no on-screen controller, keys, terminal); decoded by
    // the processor: arcade videos are small, and setting up the graphics card's decoder for each
    // file made them start later
    const QByteArray wid = QByteArray::number(qint64(winId()));
    const int widSet = gApi.set_option_string(m, "wid", wid.constData());
    DiagnosticLog::write("video", QString("mpv into window %1 (%2)").arg(QString::fromLatin1(wid)).arg(widSet));
    // only video outputs that draw into the given window (not sdl's, or a terminal's)
#ifdef Q_OS_WIN
    gApi.set_option_string(m, "vo", "gpu,direct3d");
#else
    gApi.set_option_string(m, "vo", "gpu,xv,x11");
#endif
    for (auto [k, v] : {std::pair{"osc", "no"}, std::pair{"input-default-bindings", "no"}, std::pair{"input-vo-keyboard", "no"},
                        std::pair{"input-cursor", "no"}, std::pair{"cursor-autohide", "no"}, std::pair{"terminal", "no"},
                        std::pair{"osd-level", "0"}, std::pair{"keep-open", "no"}, std::pair{"hwdec", "no"},
                        std::pair{"idle", "yes"}, std::pair{"force-window", "yes"},
                        std::pair{"audio-fallback-to-null", "yes"}}) // (no sound device: the video plays, silent)
        gApi.set_option_string(m, k, v);
    const int r = gApi.initialize(m);
    if (r < 0) {
        gError = tr("mpv couldn't be started: %1").arg(gApi.error_string ? QString::fromUtf8(gApi.error_string(r)) : QString::number(r));
        gApi.terminate_destroy(m);
        return false;
    }
    mpv_ = m;
    gApi.set_wakeup_callback(mpv_, &VideoPanel::wakeup, this);
    return true;
}

// called by mpv on its own thread: the events are read on the GUI thread
void VideoPanel::wakeup(void* self) {
    QMetaObject::invokeMethod(static_cast<VideoPanel*>(self), [p = static_cast<VideoPanel*>(self)] { p->drainEvents(); },
                              Qt::QueuedConnection);
}

void VideoPanel::drainEvents() {
    if (!mpv_) return;
    for (;;) {
        mpv_event* e = gApi.wait_event(mpv_, 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id == MPV_EVENT_FILE_LOADED) {
            passMouseToPanel();
            QTimer::singleShot(300, this, [this] { passMouseToPanel(); });
        }
        if (e->event_id == MPV_EVENT_END_FILE && playing_) {
            auto* end = static_cast<mpv_event_end_file*>(e->data);
            if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
                const QString why = gApi.error_string ? QString::fromUtf8(gApi.error_string(end->error)) : QString::number(end->error);
                DiagnosticLog::write("video", "mpv couldn't play " + QDir::toNativeSeparators(file_) + ": " + why);
                playing_ = false;
                hide();
                emit failed(why);
                emit stopped();
            } else if (end && end->reason == MPV_END_FILE_REASON_EOF) { // (with Loop on, mpv starts it again itself)
                playing_ = false;
                hide();
                emit stopped();
            }
        }
    }
}

// mpv's window inside this one, made deaf to the mouse: a disabled child window's clicks go to its
// parent (this widget), so a click stops the video and a right-click opens its menu
void VideoPanel::passMouseToPanel() {
#ifdef Q_OS_WIN
    if (!mpv_) return;
    EnumChildWindows(reinterpret_cast<HWND>(winId()), [](HWND h, LPARAM) -> BOOL {
        EnableWindow(h, FALSE);
        return TRUE;
    }, 0);
#endif
}

void VideoPanel::command(const QStringList& args) {
    if (!mpv_) return;
    std::vector<QByteArray> keep;
    std::vector<const char*> argv;
    for (const QString& a : args) keep.push_back(a.toUtf8());
    for (const QByteArray& b : keep) argv.push_back(b.constData());
    argv.push_back(nullptr);
    gApi.command(mpv_, argv.data());
}

bool VideoPanel::play(const QString& file, const Options& o) {
    if (parentWidget()) setGeometry(parentWidget()->rect());
    show();
    raise();
    if (!ensureMpv()) {
        hide();
        emit failed(gError);
        return false;
    }
    file_ = file;
    playing_ = true;
    paused_ = o.paused;
    DiagnosticLog::write("video", QString("%5 %1 (sound %2, volume %3, loop %4)")
                                      .arg(QDir::toNativeSeparators(file)).arg(o.sound).arg(o.volume).arg(o.loop)
                                      .arg(o.paused ? "cue" : "play"));
    if (gStandIn) {
        if (!standInLabel_) {
            standInLabel_ = new QLabel(this);
            standInLabel_->setObjectName("VideoPanelStandIn");
            standInLabel_->setAlignment(Qt::AlignCenter);
            standInLabel_->setStyleSheet("color: white;");
            standInLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
            auto* lay = new QVBoxLayout(this);
            lay->addWidget(standInLabel_);
        }
        standInLabel_->setText(QFileInfo(file).fileName() + (o.paused ? " (paused)" : ""));
        return true;
    }
    gApi.set_property_string(mpv_, "pause", o.paused ? "yes" : "no");
    gApi.set_property_string(mpv_, "mute", o.sound ? "no" : "yes");
    gApi.set_property_string(mpv_, "volume", QByteArray::number(o.volume).constData());
    gApi.set_property_string(mpv_, "loop-file", o.loop ? "inf" : "no");
    command({"loadfile", QDir::toNativeSeparators(file)});
    return true;
}

void VideoPanel::resume() {
    if (!playing_ || !paused_) return;
    paused_ = false;
    DiagnosticLog::write("video", "resume " + QDir::toNativeSeparators(file_));
    if (gStandIn) {
        if (standInLabel_) standInLabel_->setText(QFileInfo(file_).fileName());
        return;
    }
    if (mpv_) gApi.set_property_string(mpv_, "pause", "no");
}

void VideoPanel::stop() {
    if (!playing_ && isHidden()) return;
    playing_ = false;
    paused_ = false;
    if (mpv_) command({"stop"});
    hide();
}

bool VideoPanel::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(o, e);
}

void VideoPanel::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        stop();
        emit stopped();
        return;
    }
    // the right button itself: on Windows, mpv's window (turned off for the mouse) passes the
    // click on, but no "context menu" follows it
    if (e->button() == Qt::RightButton) {
        emit contextMenuWanted(e->globalPosition().toPoint());
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void VideoPanel::contextMenuEvent(QContextMenuEvent* e) {
    if (e->reason() == QContextMenuEvent::Keyboard) emit contextMenuWanted(mapToGlobal(rect().center())); // (the Menu key)
    // (a mouse one: the right button's release opened it already)
}

// the wheel changes the picture panel's category or layout: the video stops, the wheel goes on to it
void VideoPanel::wheelEvent(QWheelEvent* e) {
    stop();
    emit stopped();
    e->ignore();
}

} // namespace eld
