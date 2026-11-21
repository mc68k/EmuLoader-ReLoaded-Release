#include "VideoPreview.h"

#include <algorithm>
#include "WinIni.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace eld {

namespace {
// videopreview[default].ini [Parameters]
const char* kKnownPlayers[][2] = {{"mpc-hc.exe", "%1 /play /close"}, {"mpc-hc64.exe", "%1 /play /close"},
                                  {"wmplayer.exe", "%1 /play /close"}, {"vlc.exe", "--play-and-exit %1"},
                                  {"bsplayer.exe", "%1 -eend"},       {"zplayer.exe", "/CLOSE %1"}};

// ReadVideoPreviewIni: "%1" is put first when it's missing; quotes around it go (they're added).
QString fixParameters(QString p) {
    p = p.trimmed();
    if (p.isEmpty()) return QStringLiteral("%1");
    p.replace("\"%1\"", "%1", Qt::CaseInsensitive);
    if (!p.contains("%1")) p = "%1 " + p;
    return p;
}

// FindFirstW(<base>.*): a file named <base> with any extension (the first one, by name).
QString anyExtension(const QString& dir, const QString& base, bool picturesToo = true) {
    QDir d(dir);
    if (dir.isEmpty() || !d.exists()) return {};
    QStringList found = d.entryList({base + ".*"}, QDir::Files, QDir::Name);
    for (const QString& f : found) {
        QString ext = QFileInfo(f).suffix().toLower();
        if (!picturesToo && (ext == "png" || ext == "jpg" || ext == "gif")) continue; // VideoPreviewValidateSnapFolder
        return d.filePath(f);
    }
    return {};
}
} // namespace

VideoPreviewSettings VideoPreviewSettings::load(const QString& file) {
    VideoPreviewSettings s;
    WinIni ini;
    ini.load(file);
    for (const QString& key : ini.keys("Arcade Folders")) s.folders.insert(key.toLower(), ini.readString("Arcade Folders", key));
    s.player = ini.readString("MediaPlayer", "File");
    s.enabled = ini.readBool("MediaPlayer", "Enabled", true);
    s.autoPlay = ini.readBool("MediaPlayer", "AutoPlay", false);
    s.parentVideo = ini.readBool("MediaPlayer", "ParentGameVideo", false);
    s.playDummy = ini.readBool("MediaPlayer", "PlayDummyVideo", false);
    s.dummyParameters = ini.readString("MediaPlayer", "DummyVideoParameters");
    s.dummyParameters.replace("\"%1\"", "%1", Qt::CaseInsensitive);
    s.parameters = parametersFor(file, s.player);
    s.internal = ini.readBool("EmuLoaderRL", "InternalPlayer", true);
    s.sound = ini.readBool("EmuLoaderRL", "Sound", true);
    s.loop = ini.readBool("EmuLoaderRL", "Loop", true);
    s.volume = std::clamp(ini.readInteger("EmuLoaderRL", "Volume", 50), 0, 100);
    return s;
}

QString VideoPreviewSettings::parametersFor(const QString& file, const QString& player) {
    QString name = QFileInfo(QString(player).replace('\\', '/')).fileName();
    if (name.isEmpty()) return QStringLiteral("%1");
    WinIni ini;
    ini.load(file);
    QString p = ini.readString("Parameters", name);
    if (p.isEmpty())
        for (const auto& k : kKnownPlayers)
            if (name.compare(k[0], Qt::CaseInsensitive) == 0) p = k[1];
    return fixParameters(p);
}

// UpdateVideoPreviewIni
bool VideoPreviewSettings::save(const QString& file) const {
    WinIni ini;
    ini.load(file);
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        if (it.value().trimmed().isEmpty()) ini.deleteKey("Arcade Folders", it.key());
        else ini.writeString("Arcade Folders", it.key(), it.value().trimmed());
    }
    ini.writeString("MediaPlayer", "File", player);
    ini.writeInteger("MediaPlayer", "Enabled", enabled ? 1 : 0);
    ini.writeInteger("MediaPlayer", "AutoPlay", autoPlay ? 1 : 0);
    ini.writeInteger("MediaPlayer", "ParentGameVideo", parentVideo ? 1 : 0);
    ini.writeInteger("MediaPlayer", "PlayDummyVideo", playDummy ? 1 : 0);
    ini.writeString("MediaPlayer", "DummyVideoParameters", dummyParameters);
    ini.writeInteger("EmuLoaderRL", "InternalPlayer", internal ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "Sound", sound ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "Loop", loop ? 1 : 0);
    ini.writeInteger("EmuLoaderRL", "Volume", volume);
    QString name = QFileInfo(QString(player).replace('\\', '/')).fileName();
    if (!name.isEmpty()) ini.writeString("Parameters", name, fixParameters(parameters));
    QDir().mkpath(QFileInfo(file).absolutePath());
    return ini.save(file);
}

namespace VideoPreview {

QString find(const QString& folder, const QStringList& snapFolders, const QString& mameFolder,
             const QString& name, const QString& parent) {
    // VideoPreviewFindFile: the system's folder, then its videosnaps\ (the parent's after each)
    if (!folder.isEmpty())
        for (const QString& dir : {folder, QDir(folder).filePath("videosnaps")}) {
            if (QString f = anyExtension(dir, name); !f.isEmpty()) return f;
            if (!parent.isEmpty())
                if (QString f = anyExtension(dir, parent); !f.isEmpty()) return f;
        }
    // VideoPreviewFindFile_MAME
    auto in = [&](const QString& n) -> QString {
        for (const QString& snap : snapFolders) {
            if (QString f = anyExtension(QDir(snap).filePath("videosnaps"), n); !f.isEmpty()) return f;
            if (QString f = anyExtension(QDir(snap).filePath("videosnaps/videosnaps"), n); !f.isEmpty()) return f;
        }
        if (!mameFolder.isEmpty())
            if (QString f = anyExtension(QDir(mameFolder).filePath("videosnaps"), n); !f.isEmpty()) return f;
        for (const QString& snap : snapFolders) // mixed with the pictures
            if (QString f = anyExtension(snap, n, false); !f.isEmpty()) return f;
        return {};
    };
    QString f = in(name);
    if (f.isEmpty() && !parent.isEmpty()) f = in(parent);
    return f;
}

QStringList arguments(const QString& parameters, const QString& file) {
    QString p = parameters.trimmed().isEmpty() ? QStringLiteral("%1") : parameters;
    QStringList out;
    for (QString a : QProcess::splitCommand(p)) out << a.replace("%1", file, Qt::CaseInsensitive);
    return out;
}

} // namespace VideoPreview

} // namespace eld
