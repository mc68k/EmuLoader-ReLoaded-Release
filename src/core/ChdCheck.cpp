#include "ChdCheck.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace eld {

namespace {
quint32 be32(const QByteArray& b, int at) {
    return (quint32(quint8(b[at])) << 24) | (quint32(quint8(b[at + 1])) << 16) | (quint32(quint8(b[at + 2])) << 8) | quint32(quint8(b[at + 3]));
}
QString sha1At(const QByteArray& b, int at) {
    const QByteArray s = b.mid(at, 20);
    if (s.size() != 20 || s.count('\0') == 20) return {};
    return QString::fromLatin1(s.toHex());
}
} // namespace

ChdCheck::Header ChdCheck::read(const QString& file) {
    Header h;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return h;
    const QByteArray b = f.read(124);
    if (b.size() < 16 || !b.startsWith("MComprHD")) return h;
    h.version = int(be32(b, 12));
    int sha = -1, parent = -1;
    switch (h.version) {
    case 5: sha = 84; parent = 104; break;
    case 4: sha = 48; parent = 68; break;
    case 3: sha = 80; parent = 100; break;
    default: return h;
    }
    if (b.size() < parent + 20) return h;
    h.sha1 = sha1At(b, sha);
    h.parentSha1 = sha1At(b, parent);
    h.ok = !h.sha1.isEmpty();
    return h;
}

QString ChdCheck::find(const QStringList& romFolders, const QString& set, const QString& parent, const QString& name) {
    for (const QString& from : {set, parent}) {
        if (from.isEmpty()) continue;
        for (const QString& folder : romFolders) {
            const QString f = QDir(folder).filePath(from + "/" + name + ".chd");
            if (QFileInfo::exists(f)) return f;
        }
    }
    return {};
}

QHash<QString, QString> ChdCheck::index(const QStringList& romFolders) {
    QHash<QString, QString> out;
    for (const QString& folder : romFolders) {
        QDirIterator sets(folder, QDir::Dirs | QDir::NoDotAndDotDot);
        while (sets.hasNext()) {
            QDirIterator chds(sets.next(), {"*.chd"}, QDir::Files);
            while (chds.hasNext()) {
                const QString f = chds.next();
                const Header h = read(f);
                if (h.ok && !out.contains(h.sha1)) out.insert(h.sha1, f);
            }
        }
    }
    return out;
}

QString ChdCheck::copyCommand(const QString& chd, const QString& parent) {
    const QFileInfo fi(chd);
    const QString out = QDir(fi.absolutePath()).filePath(fi.completeBaseName() + "-full.chd");
    auto q = [](const QString& p) { const QString n = QDir::toNativeSeparators(p); return n.contains(' ') ? '"' + n + '"' : n; };
    return QStringLiteral("chdman copy -i %1 -ip %2 -o %3").arg(q(chd), q(parent), q(out));
}

} // namespace eld
