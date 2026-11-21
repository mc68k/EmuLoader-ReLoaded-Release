#include "DiagnosticLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <QThread>

namespace eld::DiagnosticLog {

namespace {
constexpr qint64 kMaxSize = 5 * 1024 * 1024; // MaxLogSize

struct State {
    QMutex lock;
    QString file, version;
    bool preference = false, commandLine = false, on = false, failed = false, started = false;
    std::function<void(const QString&, const QString&)> failure;
    QtMessageHandler previous = nullptr;
};

State& state() {
    static State s;
    return s;
}

thread_local bool inHandler = false;

// AppendLine: one line, its line ends shown as \r \n; the file moved to .1 past 5 MB
QString append(State& s, const QString& category, const QString& message) {
    QString text = message.left(4096);
    text.replace('\r', QLatin1String("\\r")).replace('\n', QLatin1String("\\n"));
    const QByteArray line = (QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") +
                             QString(" [pid=%1 tid=%2] [%3] ").arg(QCoreApplication::applicationPid())
                                 .arg(quintptr(QThread::currentThreadId())).arg(category) + text + "\r\n").toUtf8();
    if (QFileInfo(s.file).size() + line.size() > kMaxSize) {
        const QString backup = s.file + ".1";
        if (QFile::exists(backup) && !QFile::remove(backup)) return QStringLiteral("could not delete ") + QDir::toNativeSeparators(backup);
        if (!QFile::rename(s.file, backup)) return QStringLiteral("could not rename the log to ") + QDir::toNativeSeparators(backup);
    }
    QDir().mkpath(QFileInfo(s.file).absolutePath());
    QFile f(s.file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return f.errorString();
    if (f.write(line) != line.size()) return f.errorString();
    return {};
}

const char* typeName(QtMsgType t) {
    switch (t) {
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    case QtInfoMsg: return "info";
    default: return "debug";
    }
}

// Qt's warnings go in the log too (and on to where they went before)
void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    State& s = state();
    if (type != QtDebugMsg && !inHandler && enabled()) {
        inHandler = true;
        write(QStringLiteral("qt"), QString("%1: %2").arg(typeName(type), msg) + (ctx.category && qstrcmp(ctx.category, "default") != 0 ? QString(" (%1)").arg(ctx.category) : QString()));
        inHandler = false;
    }
    if (s.previous) s.previous(type, ctx, msg);
}

void sessionLine(State& s) { // the first line of a session, once logging is on
    if (s.started || !s.on) return;
    s.started = true;
    const QString os = QSysInfo::prettyProductName() + " (" + QSysInfo::currentCpuArchitecture() + ")";
    const QString why = s.commandLine && !s.preference ? QStringLiteral("--debug-log") : QStringLiteral("Preferences");
    append(s, QStringLiteral("session"), QString("%1 started; Qt %2; %3; logging on (%4); folder=%5")
                                             .arg(s.version, QString::fromLatin1(qVersion()), os, why,
                                                  QDir::toNativeSeparators(QCoreApplication::applicationDirPath())));
}
} // namespace

void open(const QString& file, const QString& version, bool preference, bool commandLine) {
    State& s = state();
    {
        QMutexLocker l(&s.lock);
        if (s.file != file) s.started = false; // another data folder: a new session line there
        s.file = file;
        s.version = version;
        s.preference = preference;
        s.commandLine = s.commandLine || commandLine; // --debug-log stays
        s.on = (preference || commandLine) && !s.failed;
        sessionLine(s);
    }
    if (!s.previous) s.previous = qInstallMessageHandler(handler);
}

void setPreference(bool on) {
    State& s = state();
    QMutexLocker l(&s.lock);
    if (s.preference == on) return;
    const bool was = s.on;
    s.preference = on;
    if (was && !(on || s.commandLine)) append(s, QStringLiteral("session"), QStringLiteral("Diagnostic logging disabled in preferences"));
    s.on = (on || s.commandLine) && !s.failed && !s.file.isEmpty();
    if (s.on && !was) {
        if (s.started) append(s, QStringLiteral("session"), QStringLiteral("Diagnostic logging enabled in preferences"));
        else sessionLine(s);
    }
}

bool enabled() {
    State& s = state();
    QMutexLocker l(&s.lock);
    return s.on;
}

QString file() {
    State& s = state();
    QMutexLocker l(&s.lock);
    return s.file;
}

void write(const QString& category, const QString& message) {
    State& s = state();
    QString error;
    std::function<void(const QString&, const QString&)> failure;
    QString f;
    {
        QMutexLocker l(&s.lock);
        if (!s.on) return;
        error = append(s, category, message);
        if (error.isEmpty()) return;
        s.on = false; // logging stops: it can't be written
        s.failed = true;
        failure = s.failure;
        f = s.file;
    }
    if (failure) failure(f, error);
}

void onFailure(const std::function<void(const QString&, const QString&)>& f) {
    State& s = state();
    QMutexLocker l(&s.lock);
    s.failure = f;
}

void close() {
    write(QStringLiteral("session"), QStringLiteral("EmuLoader closed"));
    State& s = state();
    QMutexLocker l(&s.lock);
    s.on = false;
}

} // namespace eld::DiagnosticLog
