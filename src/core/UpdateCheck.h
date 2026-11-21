// UpdateCheck - is there a newer EmuLoader ReLoaded? The latest release of the public release
// repository on GitHub (its tag, "v1.0.0"), compared with the running version. Only the version
// number is asked for: nothing about the user or their games is sent. Nothing is downloaded or
// installed; the user is offered the release's page.
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
    bool prerelease = false;
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
