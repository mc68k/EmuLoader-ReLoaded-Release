#include "UpdateCheck.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace eld {

namespace UpdateCheck {

QString defaultUrl() { return QStringLiteral("https://api.github.com/repos/mc68k/EmuLoader-ReLoaded-Release/releases/latest"); }

QString url() {
    const QString over = qEnvironmentVariable("EMULOADER_UPDATE_URL");
    return over.isEmpty() ? defaultUrl() : over;
}

Answer parse(const QByteArray& json, Release* out, QString* error) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) {
        if (error) *error = json.trimmed().isEmpty() ? QStringLiteral("an empty answer") : QStringLiteral("an answer that isn't JSON");
        return Answer::Unreadable;
    }
    const QJsonObject o = doc.object();
    const QString tag = o.value("tag_name").toString();
    if (tag.isEmpty()) {
        const QString message = o.value("message").toString();
        if (message.compare("Not Found", Qt::CaseInsensitive) == 0) return Answer::NoneYet;
        if (error) *error = message.isEmpty() ? QStringLiteral("no version in the answer") : message;
        return Answer::Unreadable;
    }
    if (out) {
        out->tag = tag;
        out->name = o.value("name").toString();
        out->page = o.value("html_url").toString();
        out->notes = o.value("body").toString();
        out->prerelease = o.value("prerelease").toBool();
        out->zip = {};
        for (const QJsonValue& v : o.value("assets").toArray()) {
            const QJsonObject a = v.toObject();
            const QString name = a.value("name").toString();
            if (!name.endsWith("-windows.zip", Qt::CaseInsensitive)) continue;
            out->zip.name = name;
            out->zip.url = a.value("browser_download_url").toString();
            out->zip.size = qint64(a.value("size").toDouble());
            const QString digest = a.value("digest").toString();
            if (digest.startsWith("sha256:", Qt::CaseInsensitive)) out->zip.sha256 = digest.mid(7).toLower();
            break;
        }
    }
    return Answer::Release;
}

Version parseVersion(const QString& text) {
    static const QRegularExpression re("^\\s*[vV]?(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?\\s*(.*)$");
    const QRegularExpressionMatch m = re.match(text);
    Version v;
    if (!m.hasMatch()) return v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.patch = m.captured(3).toInt();
    const QString rest = m.captured(4).trimmed();
    v.pre = !rest.isEmpty() && !rest.startsWith('+'); // ("+build" is no pre-release)
    v.valid = true;
    return v;
}

int compare(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre != b.pre) return a.pre ? -1 : 1;
    return 0;
}

bool newer(const QString& release, const QString& running) {
    const Version r = parseVersion(release), v = parseVersion(running);
    return r.valid && v.valid && compare(r, v) > 0;
}

} // namespace UpdateCheck

} // namespace eld
