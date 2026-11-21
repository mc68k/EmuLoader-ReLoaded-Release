#include "CrashLog.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#include <cstdio>
#include <cwchar>
#include <exception>

#ifndef EL_BUILD_REV
#define EL_BUILD_REV "dev"
#endif

namespace eld {

namespace {
wchar_t gFile[MAX_PATH * 2] = {0};
volatile LONG gWriting = 0;

// Plain Win32 only from here: the program may be in a bad state.
void put(HANDLE h, const char* s) {
    DWORD n = 0;
    WriteFile(h, s, DWORD(strlen(s)), &n, nullptr);
}

void frame(HANDLE h, int i, DWORD64 pc) {
    char line[MAX_PATH + 80];
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(pc), &mod) && mod) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(mod, path, MAX_PATH)) {
            const char* slash = strrchr(path, '\\');
            snprintf(name, sizeof name, "%s", slash ? slash + 1 : path);
        }
    }
    snprintf(line, sizeof line, "  #%-2d %s+0x%llx\r\n", i, name,
             static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(mod)));
    put(h, line);
}

// The stack, from a context (x64 unwinding: no debug files needed on the user's computer)
void stack(HANDLE h, CONTEXT ctx) {
    put(h, "Stack:\r\n");
    for (int i = 0; i < 48 && ctx.Rip; ++i) {
        frame(h, i, ctx.Rip);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if (!fn) { // a leaf function: the return address is on top of the stack
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        PVOID data = nullptr;
        DWORD64 frameBase = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &data, &frameBase, nullptr);
    }
}

void write(const char* what, const CONTEXT* ctx, const EXCEPTION_RECORD* rec) {
    if (InterlockedExchange(&gWriting, 1)) return; // once
    if (!gFile[0]) return;
    HANDLE h = CreateFileW(gFile, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[512];
    snprintf(line, sizeof line, "%s %s (build %s) stopped: %s\r\n%04d-%02d-%02d %02d:%02d:%02d\r\n", EL_PRODUCT_NAME,
             EL_VERSION_LABEL, EL_BUILD_REV, what, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    put(h, line);
    if (rec) {
        snprintf(line, sizeof line, "Exception 0x%08lx", static_cast<unsigned long>(rec->ExceptionCode));
        put(h, line);
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
            snprintf(line, sizeof line, " (%s address 0x%llx)",
                     rec->ExceptionInformation[0] == 0 ? "reading" : rec->ExceptionInformation[0] == 1 ? "writing" : "running",
                     static_cast<unsigned long long>(rec->ExceptionInformation[1]));
        else line[0] = 0;
        put(h, line);
        put(h, "\r\n");
    }
    if (ctx) stack(h, *ctx);
    put(h, "\r\nPlease send this file along with what you were doing when it happened.\r\n");
    CloseHandle(h);
}

LONG WINAPI onException(EXCEPTION_POINTERS* e) {
    write("crash", e->ContextRecord, e->ExceptionRecord);
    return EXCEPTION_CONTINUE_SEARCH; // Windows' own report too
}

void here(const char* what) {
    CONTEXT ctx;
    RtlCaptureContext(&ctx);
    write(what, &ctx, nullptr);
}

void onAbort(int) { here("abort (a fatal error)"); }
void onTerminate() {
    here("an unhandled C++ exception");
    std::abort();
}
} // namespace

void installCrashLog(const QString& file) {
    const std::wstring w = QDir::toNativeSeparators(file).toStdWString();
    wcsncpy(gFile, w.c_str(), sizeof gFile / sizeof gFile[0] - 1);
    static bool installed = false;
    if (installed) return;
    installed = true;
    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onAbort);
}

} // namespace eld

#else

namespace eld {
void installCrashLog(const QString&) {}
} // namespace eld

#endif
