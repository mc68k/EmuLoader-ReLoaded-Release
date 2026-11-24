// UpdateCheck - is there a newer EmuLoader ReLoaded? The latest release of the public release
// repository on GitHub (its tag, "v1.0.0"), compared with the running version. Only the version
// number is asked for: nothing about the user or their games is sent. The answer also gives the
// release's notes and its Windows zip, which Install Update downloads (UpdateInstall).
#pragma once

#include <QByteArray>
#include <QString>

namespace eld {

namespace UpdateCheck {

// GitHub's latest release of the release repository (drafts and pre-releases aren't "latest")
QString defaultUrl();
// The address asked: EMULOADER_UPDATE_URL when set (a file), else defaultUrl()
QString url();

struct Release {
    QString tag;      // "v1.1.0"
    QString name;     // the release's title ("EmuLoader ReLoaded 1.1")
    QString page;     // the release's page (html_url)
    QString notes;    // what's new (the release's text, Markdown)
    bool prerelease = false;
    // the Windows zip ("EmuLoader-ReLoaded-1.2.0-windows.zip"): empty url when there's none
    struct Zip {
        QString name;
        QString url;     // browser_download_url
        qint64 size = 0;
        QString sha256;  // lowercase hex, from GitHub's "digest" ("sha256:..."); empty when not given
    } zip;
};

enum class Answer { Release, NoneYet, Unreadable };
// GitHub's answer: a release, "Not Found" (none published yet), or something else (error says what)
Answer parse(const QByteArray& json, Release* out, QString* error = nullptr);

// The numbers of a version ("v1.2.3", "1.2", "1.0 beta", "v1.1.0-rc1") and whether it's a
// pre-release (a word after the numbers: "beta", "-rc1")
struct Version {
    int major = 0, minor = 0, patch = 0;
    bool pre = false;
    bool valid = false;
};
Version parseVersion(const QString& text);
// <0, 0 or >0. Same numbers: a release is newer than a pre-release of them (1.0.0 > 1.0 beta).
int compare(const Version& a, const Version& b);
// Is `release` newer than `running`?
bool newer(const QString& release, const QString& running);

} // namespace UpdateCheck

} // namespace eld
