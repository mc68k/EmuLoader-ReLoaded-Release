#include "AboutDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace eld {

namespace {

const char* const kReleases = "https://github.com/mc68k/EmuLoader-ReLoaded-Release";
const char* const kIssues = "https://github.com/mc68k/EmuLoader-ReLoaded-Release/issues";

QLabel* heading(const QString& text) {
    auto* l = new QLabel(text.toUpper());
    QFont f = l->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 0.9);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    l->setFont(f);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

QString folderLink(const QString& path) {
    return QStringLiteral("<a href=\"%1\">%2</a>")
        .arg(QUrl::fromLocalFile(path).toString().toHtmlEscaped(), QDir::toNativeSeparators(path).toHtmlEscaped());
}

} // namespace

QString AboutDialog::systemLine() {
    QString os = QSysInfo::prettyProductName();
#ifdef Q_OS_WIN
    // Qt 6.4 calls Windows 11 "Windows 10": the build number tells them apart
    bool ok = false;
    const int build = QSysInfo::kernelVersion().section('.', 2, 2).toInt(&ok);
    if (ok && build >= 22000) os.replace("Windows 10", "Windows 11");
#endif
    return QStringLiteral("%1 (%2, %3)").arg(os, QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
}

AboutDialog::AboutDialog(const Input& in, QWidget* parent) : QDialog(parent) {
    setObjectName("AboutDialog");
    setWindowTitle(tr("About %1").arg(QStringLiteral(EL_PRODUCT_NAME)));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    const QString muted = palette().color(QPalette::PlaceholderText).name();
    const QString link = palette().color(QPalette::Link).name();
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(14);

    // the icon, the name, what it is, the version
    auto* top = new QHBoxLayout;
    top->setSpacing(20);
    auto* icon = new QLabel;
    const QIcon ico(in.icon);
    if (!ico.isNull()) icon->setPixmap(ico.pixmap(QSize(96, 96)));
    icon->setObjectName("AboutIcon");
    icon->setFixedSize(96, 96); // all of it: a label's size can follow the text's
    top->addWidget(icon, 0, Qt::AlignTop);
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    auto* title = new QLabel(QStringLiteral(EL_PRODUCT_NAME));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 2.1);
    tf.setBold(true);
    title->setFont(tf);
    names->addWidget(title);
    auto* what = new QLabel(tr("A front end for MAME and HBMAME"));
    QFont wf = what->font();
    wf.setPointSizeF(wf.pointSizeF() * 1.1);
    what->setFont(wf);
    what->setStyleSheet("color: " + muted);
    names->addWidget(what);
    auto* ver = new QLabel(tr("Version %1, build %2").arg(QStringLiteral(EL_VERSION_LABEL), QStringLiteral(EL_BUILD_REV)));
    ver->setObjectName("AboutVersion");
    ver->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ver->setContentsMargins(0, 8, 0, 0);
    names->addWidget(ver);
    names->addStretch(1);
    top->addLayout(names, 1);
    lay->addLayout(top);

    auto* credit = new QLabel(tr("A community continuation of EmuLoader, originally created by Ciro Alfredo Consentino "
                                 "(1974–2021). Ported from his Emuloader v8.9.4."));
    credit->setWordWrap(true);
    lay->addWidget(credit);

    // the links
    auto* links = new QGridLayout;
    links->setHorizontalSpacing(8);
    int col = 0;
    auto button = [&](const QString& text, const char* name, const std::function<void()>& fn) {
        auto* b = new QPushButton(text);
        b->setObjectName(name);
        b->setAutoDefault(false);
        b->setMinimumHeight(30);
        connect(b, &QPushButton::clicked, this, fn);
        links->addWidget(b, 0, col);
        links->setColumnStretch(col++, 1);
    };
    button(tr("GitHub Page"), "AboutGitHub", [] { QDesktopServices::openUrl(QUrl(kReleases)); });
    button(tr("Report a Problem"), "AboutReport", [] { QDesktopServices::openUrl(QUrl(kIssues)); });
    button(tr("User Guide"), "AboutGuide", [fn = in.openGuide] {
        if (fn) fn();
    });
    button(tr("Check for Updates"), "AboutUpdates", [this, fn = in.checkForUpdates] {
        if (!fn) return;
        accept(); // the update check's message takes over
        QTimer::singleShot(0, fn);
    });
    lay->addLayout(links);

    // the setup, for problem reports (no folders: they can hold a person's name)
    lay->addWidget(heading(tr("Your setup")));
    info_ = QStringLiteral(EL_PRODUCT_NAME " " EL_VERSION_LABEL " (build " EL_BUILD_REV ")\n") + systemLine() + "\n" +
            QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion())) + "\n" +
            (in.mame.isEmpty() ? QStringLiteral("MAME: not set up") : in.mame);
    if (!in.hbmame.isEmpty()) info_ += "\n" + in.hbmame;
    auto* box = new QFrame;
    box->setFrameShape(QFrame::StyledPanel);
    box->setAutoFillBackground(true);
    box->setBackgroundRole(QPalette::Base);
    auto* boxLay = new QHBoxLayout(box);
    boxLay->setContentsMargins(12, 10, 12, 10);
    boxLay->setSpacing(12);
    auto* infoText = new QLabel(info_);
    infoText->setObjectName("AboutVersionInfo");
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(infoText->font().pointSizeF());
    infoText->setFont(mono);
    infoText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    boxLay->addWidget(infoText, 1);
    auto* copy = new QPushButton(tr("Copy Version Info"));
    copy->setObjectName("AboutCopy");
    copy->setAutoDefault(false);
    copy->setMinimumHeight(30);
    connect(copy, &QPushButton::clicked, this, [this, copy] {
        QApplication::clipboard()->setText(info_);
        const QString was = copy->text();
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy, was] { copy->setText(was); });
    });
    boxLay->addWidget(copy, 0, Qt::AlignTop);
    lay->addWidget(box);
    auto* hint = new QLabel(tr("Paste this into a problem report."));
    hint->setStyleSheet("color: " + muted);
    lay->addWidget(hint);

    // the folders
    lay->addWidget(heading(tr("Folders")));
    auto* folders = new QLabel(tr("Data folder: %1").arg(folderLink(in.dataFolder)) + "<br>" +
                               tr("Settings folder: %1").arg(folderLink(in.settingsFolder)));
    folders->setObjectName("AboutFolders");
    folders->setTextFormat(Qt::RichText);
    folders->setOpenExternalLinks(true);
    folders->setTextInteractionFlags(Qt::TextBrowserInteraction);
    folders->setWordWrap(true);
    folders->setStyleSheet("a { color: " + link + "; }");
    lay->addWidget(folders);

    // the credits
    lay->addWidget(heading(tr("Credits")));
    auto* creditsRow = new QHBoxLayout;
    creditsRow->setSpacing(12);
    auto* libs = new QLabel(tr("Built with Qt, mpv and FFmpeg, the LZMA SDK, miniz and the Press Start 2P font."));
    libs->setWordWrap(true);
    libs->setStyleSheet("color: " + muted);
    creditsRow->addWidget(libs, 1);
    auto* lic = new QPushButton(tr("Licenses..."));
    lic->setObjectName("AboutLicenses");
    lic->setAutoDefault(false);
    lic->setMinimumHeight(28);
    connect(lic, &QPushButton::clicked, this, [folder = in.licensesFolder] {
        if (QFileInfo(folder).isDir()) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        else QDesktopServices::openUrl(QUrl(QString(kReleases) + "#license-and-credits"));
    });
    creditsRow->addWidget(lic, 0, Qt::AlignVCenter);
    lay->addLayout(creditsRow);

    lay->addStretch(1);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch(1);
    auto* ok = new QPushButton(tr("OK"));
    ok->setDefault(true);
    ok->setMinimumSize(96, 30);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(ok);
    lay->addLayout(bottom);

    setMinimumWidth(640);
    adjustSize();
}

} // namespace eld
