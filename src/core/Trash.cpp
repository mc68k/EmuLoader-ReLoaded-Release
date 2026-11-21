#include "Trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace eld::trash {

#ifdef Q_OS_WIN
static bool underWine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version");
}
#endif

bool systemAsks() {
#ifdef Q_OS_WIN
    return !underWine(); // Wine's Recycle Bin is the desktop's: the program asks itself
#else
    return false;
#endif
}

QStringList recycle(const QStringList& files, quintptr owner, const std::function<bool(int, int)>& progress) {
    QStringList gone;
#ifdef Q_OS_WIN
    if (systemAsks()) {
        const int chunk = 20; // a few at a time: quicker than one by one, and the progress moves
        for (int i = 0; i < files.size(); i += chunk) {
            if (progress && !progress(i, int(files.size()))) break;
            std::wstring list; // "a\0b\0\0"
            QStringList part;
            for (int j = i; j < files.size() && j < i + chunk; ++j) {
                if (!QFile::exists(files[j])) continue;
                part << files[j];
                list += QDir::toNativeSeparators(QFileInfo(files[j]).absoluteFilePath()).toStdWString();
                list.push_back(L'\0');
            }
            if (part.isEmpty()) continue;
            list.push_back(L'\0');
            SHFILEOPSTRUCTW op{};
            op.hwnd = reinterpret_cast<HWND>(owner);
            op.wFunc = FO_DELETE;
            op.pFrom = list.c_str();
            op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT | FOF_NOERRORUI;
            SHFileOperationW(&op);
            for (const QString& f : part)
                if (!QFile::exists(f)) gone << f;
            if (op.fAnyOperationsAborted) break; // Cancel in Windows' question
        }
        return gone;
    }
#else
    Q_UNUSED(owner);
#endif
    for (int i = 0; i < files.size(); ++i) {
        if (progress && i % 10 == 0 && !progress(i, int(files.size()))) break;
        if (QFile::exists(files[i]) && QFile::moveToTrash(files[i])) gone << files[i];
    }
    return gone;
}

} // namespace eld::trash
