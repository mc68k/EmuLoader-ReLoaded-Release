// FileProblem - the messages for a file that couldn't be saved or a program that couldn't be
// started, the same everywhere: what happened, the likely reason (the folder isn't there, it's
// read-only, the disk is full; the reason the system gave), what to do, and the file's full path.
#pragma once

#include <QCoreApplication>
#include <QString>

class QWidget;

namespace eld {

class FileProblem {
    Q_DECLARE_TR_FUNCTIONS(FileProblem)
public:
    // Why a file probably can't be written there, in a sentence ("" when nothing is plainly wrong)
    static QString whyNotWritable(const QString& path);
    // "%1 couldn't be saved." with the reason and the path, as text (for a message made elsewhere)
    static QString cantSaveText(const QString& path);
    // "%1 couldn't be saved." with the reason, what to do and the path, as a warning
    static void cantSave(QWidget* parent, const QString& title, const QString& path);
    // "%1 couldn't be started." with the reason given, what to do and the path
    static void cantStart(QWidget* parent, const QString& title, const QString& program, const QString& reason);
};

} // namespace eld
