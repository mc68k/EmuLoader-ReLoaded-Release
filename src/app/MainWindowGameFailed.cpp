// MainWindow: a game that couldn't be played. MAME writes why it stopped to its error output (the
// ROM files it couldn't find, an unknown machine, a fatal error ...) and exits with a code that
// says what kind of problem it was; EmuLoader reads both while the game runs (runGame) and shows
// them here once the game has ended. The status bar keeps its short line too.
#include "MainWindow.h"
#include "ElWidgets.h"
#include "ui_FormMain.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QImage>
#include <QMenu>
#include <memory>
#include <QFrame>
#include <QMouseEvent>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

namespace eld {

namespace {

// What an exit code means, in a sentence (MAME's EMU_ERR_ codes)
QString explanation(const MainWindow::GameAttempt& a) {
    const QString emu = a.emulator;
    if (a.crashed)
        return MainWindow::tr("%1 stopped unexpectedly (it crashed). Running the game again, or with another "
                              "version of %1, may help.").arg(emu);
    switch (a.code) {
    case 1:
        return MainWindow::tr("%1 stopped because the game failed its validity checks. That's a problem in this "
                              "build of %1, not in your files.").arg(emu);
    case 2:
        return MainWindow::tr("%1 stopped because some of the game's ROM files are missing or wrong. Its message "
                              "below lists them.").arg(emu);
    case 3:
        return MainWindow::tr("%1 stopped because of a fatal error. Its message below says what went wrong.").arg(emu);
    case 4:
        return MainWindow::tr("%1 stopped because a device or media file (a cartridge, a disk or other software) "
                              "couldn't be loaded.").arg(emu);
    case 5:
        return MainWindow::tr("%1 doesn't know this game. The games list may come from another version of %1: "
                              "create the games list again (Menu Options > MAME/Arcade > Create Arcade Games List).")
            .arg(emu);
    case 6:
        return MainWindow::tr("%1 stopped because of a problem in its settings: mame.ini, or one of its other "
                              ".ini files.").arg(emu);
    default:
        return MainWindow::tr("%1 stopped with exit code %2.").arg(emu).arg(a.code);
    }
}

} // namespace

void MainWindow::showGameFailed(const QString& title, const QString& name, const GameAttempt& last, bool ran) {
    QDialog dlg(this);
    dlg.setObjectName(ran ? "GameWarningsMessage" : "GameFailedMessage");
    dlg.setWindowTitle(tr("Run Game"));
    auto* top = new QHBoxLayout;
    auto* icon = new QLabel;
    const int is = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
    icon->setPixmap(style()->standardIcon(ran ? QStyle::SP_MessageBoxInformation : QStyle::SP_MessageBoxWarning, nullptr, this).pixmap(is, is));
    icon->setAlignment(Qt::AlignTop);
    top->addWidget(icon);
    auto* words = new QVBoxLayout;
    const QString game = title.isEmpty() || title == name ? name : QString("%1 (%2)").arg(title, name);
    auto* heading = new QLabel(ran ? tr("%1 ran, and %2 reported problems.").arg(game, last.emulator)
                                   : tr("%1 couldn't be played.").arg(game));
    heading->setObjectName("GameFailedHeading");
    QFont hf = heading->font();
    hf.setBold(true);
    heading->setFont(hf);
    heading->setWordWrap(true);
    words->addWidget(heading);
    const QString text = ran ? tr("This is what %1 wrote to its error output while the game ran, such as artwork or other "
                                  "files it couldn't load. The game itself started normally.").arg(last.emulator)
                             : explanation(last);
    auto* explain = new QLabel(text);
    explain->setObjectName("GameFailedExplanation");
    explain->setWordWrap(true);
    words->addWidget(explain);
    top->addLayout(words, 1);

    // what it said
    const QString said = last.message;
    const bool anySaid = !said.isEmpty();
    auto* box = new QVBoxLayout(&dlg);
    box->addLayout(top);
    auto* saidLabel = new QLabel(anySaid ? tr("%1's message:").arg(last.emulator) : tr("%1 gave no message.").arg(last.emulator));
    saidLabel->setObjectName("GameFailedMessageLabel");
    box->addSpacing(6);
    box->addWidget(saidLabel);
    auto* message = new QPlainTextEdit(said);
    message->setObjectName("GameFailedMessageText");
    message->setReadOnly(true);
    message->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    message->setLineWrapMode(QPlainTextEdit::NoWrap);
    const QFontMetrics fm(message->font());
    message->setMinimumSize(fm.horizontalAdvance(QString(72, 'x')), fm.lineSpacing() * 9);
    message->setVisible(anySaid);
    box->addWidget(message, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    QPushButton* copy = buttons->addButton(tr("Copy Message"), QDialogButtonBox::ActionRole);
    copy->setObjectName("GameFailedCopy");
    connect(copy, &QPushButton::clicked, &dlg, [=] {
        QApplication::clipboard()->setText(heading->text() + "\n\n" + explain->text() + (anySaid ? "\n\n" + said : QString()));
        copy->setText(tr("Copied"));
        QTimer::singleShot(1500, copy, [copy] { copy->setText(tr("Copy Message")); });
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    box->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    buttons->button(QDialogButtonBox::Ok)->setFocus();
    dlg.exec();
}

namespace {
// The status bar's line, clicked while it still shows the note: MAME's message
class WarningsLink : public QObject {
public:
    std::function<void()> show;
    QString note, tip;
    bool eventFilter(QObject* o, QEvent* e) override {
        auto* l = static_cast<QLabel*>(o);
        // (a long line is shown shortened with "…": the whole of it is kept aside, VclRuntime)
        const QString full = l->property("elShown").toString() == l->text() ? l->property("elFull").toString() : l->text();
        const bool on = !note.isEmpty() && (full == note || l->text() == note);
        if (e->type() == QEvent::Enter) { // (a hand and a tip only while it shows the note)
            if (on) {
                l->setCursor(Qt::PointingHandCursor);
                l->setToolTip(tip);
            } else {
                l->unsetCursor();
                if (l->toolTip() == tip) l->setToolTip(QString());
            }
        } else if (e->type() == QEvent::MouseButtonRelease && on && show) {
            show();
            return true;
        }
        return false;
    }
};
} // namespace

namespace {
// An icon in gray (its transparency kept): the MAME Problems button once its message was seen
QIcon grayIcon(const QIcon& from) {
    QIcon out;
    for (const QSize& sz : from.availableSizes()) {
        QImage img = from.pixmap(sz).toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
        out.addPixmap(QPixmap::fromImage(img));
    }
    return out;
}
} // namespace

// The MAME Problems tool bar button (left of the Guide by default; Settings > Tool Bar moves or
// removes it like any other). It's on the tool bar only while MAME has reported problems with the
// game just played: it flashes from color to gray three times, a click shows MAME's message (and
// it stays gray: seen), a right-click dismisses it, and the next game takes it away.
void MainWindow::setupProblemsButton() {
    auto* b = new ElButton(ui_->ToolBarButtons);
    b->setObjectName("ButtonMameProblems");
    b->setProperty("elDesignText", tr("MAME Problems-Shows what MAME reported about the game just played"));
    b->setFlat(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet("QPushButton { border: none; background: transparent; }"
                     "QPushButton:hover { background: rgba(0, 120, 215, 30); }"
                     "QPushButton:pressed { background: rgba(0, 120, 215, 60); }");
    setToolBarIcon(b, "msgbox_icons/error.ico", false, -1);
    b->setToolTip(tr("MAME Problems: what MAME reported about the game just played"));
    connect(b, &QPushButton::clicked, this, [this, b] {
        b->setProperty("elSeen", true);
        problemsIcon();
        if (problemsShow_) problemsShow_();
    });
    auto* menu = new QMenu(b);
    menu->setObjectName("ProblemsMenu");
    menu->addAction(tr("Show the Problems"), b, &QPushButton::click);
    menu->addAction(tr("Dismiss"), this, [this] { hideGameNotice(); });
    b->setProperty("elMenu", QVariant::fromValue<QObject*>(menu)); // (the » menu)
    b->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(b, &QWidget::customContextMenuRequested, this, [b, menu] { menu->exec(b->mapToGlobal(QPoint(8, b->height()))); });
    b->hide();
}

// Its picture: in color, or in gray once seen (and every other step of the flash)
void MainWindow::problemsIcon(bool flashGray) {
    auto* b = ui_->ToolBarButtons->findChild<ElButton*>("ButtonMameProblems");
    if (!b) return;
    const QIcon color = toolbarIcon("msgbox_icons/error.ico", false, -1);
    b->setIcon(flashGray || b->property("elSeen").toBool() ? grayIcon(color) : color);
}

void MainWindow::hideGameNotice() {
    auto* b = ui_->ToolBarButtons->findChild<ElButton*>("ButtonMameProblems");
    if (!b || !b->property("elActive").toBool()) return;
    b->setProperty("elActive", false);
    problemsShow_ = nullptr;
    setToolOrder(toolOrder_, false); // (off the tool bar: the room it took goes back)
}

void MainWindow::noteGameWarnings(const QString& title, const QString& name, const GameAttempt& attempt) {
    QLabel* status = ui_->StatusBar_GamesGameName;
    auto* link = status->findChild<WarningsLink*>("GameWarningsLink");
    if (!link) {
        link = new WarningsLink;
        link->setParent(status);
        link->setObjectName("GameWarningsLink");
        status->installEventFilter(link);
    }
    int lines = 0;
    for (const QString& l : attempt.message.split('\n'))
        if (!l.trimmed().isEmpty()) ++lines;
    const QString game = title.isEmpty() ? name : title;
    link->note = lines == 1 ? tr("%1: %2 reported 1 problem. Click here to see it.").arg(game, attempt.emulator)
                            : tr("%1: %2 reported %3 problems. Click here to see them.").arg(game, attempt.emulator).arg(lines);
    link->show = [this, title, name, attempt] { showGameFailed(title, name, attempt, true); };
    link->tip = tr("Click to see what %1 reported.").arg(attempt.emulator);
    status->setText(link->note);
    status->setToolTip(link->tip);
    status->setCursor(Qt::PointingHandCursor);

    // and the MAME Problems button on the tool bar, flashing (when it's on the tool bar)
    auto* b = ui_->ToolBarButtons->findChild<ElButton*>("ButtonMameProblems");
    if (!b) return;
    const bool again = b->property("elActive").toBool() && b->property("elNote").toString() == link->note;
    problemsShow_ = link->show;
    b->setProperty("elNote", link->note);
    b->setToolTip(link->note.section(". ", 0, 0) + ".\n" + tr("Click to see what %1 reported; right-click to dismiss.").arg(attempt.emulator));
    if (again) return; // (said twice: once now, once after the list is put back)
    b->setProperty("elActive", true);
    b->setProperty("elSeen", false);
    setToolOrder(toolOrder_, false);
    problemsIcon();
    auto* flash = new QTimer(b);
    auto step = std::make_shared<int>(0);
    connect(flash, &QTimer::timeout, this, [this, flash, step] {
        ++*step;
        problemsIcon(*step % 2 == 1);
        if (*step >= 6) flash->deleteLater(); // three times gray and back
    });
    flash->start(330);
}

void MainWindow::showGameNotStarted(const QString& emulator, const QString& exe, const QString& reason) {
    QMessageBox box(QMessageBox::Warning, tr("Run Game"), tr("%1 couldn't be started.").arg(emulator), QMessageBox::Ok, this);
    box.setObjectName("GameNotStartedMessage");
    box.setInformativeText(tr("The reason given: %1").arg(reason) + "\n\n" + tr("File: %1").arg(QDir::toNativeSeparators(exe)) +
                           "\n\n" + tr("Make sure the file is still there, and that a security program isn't blocking it."));
    box.exec();
}

} // namespace eld
