#include "UpdateFetch.h"

#include <QCoreApplication>
#include <QFile>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winhttp.h>
#else
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#endif

namespace eld {

namespace {

const char* kUserAgent = EL_PRODUCT_NAME " " EL_VERSION_LABEL; // (GitHub asks for one)
constexpr int kWaitMs = 15000;

void deliver(QPointer<QObject> ctx, std::function<void(const FetchResult&)> done, FetchResult r) {
    if (!ctx) return;
    QMetaObject::invokeMethod(ctx, [ctx, done, r] {
        if (ctx) done(r);
    }, Qt::QueuedConnection);
}

bool isFile(const QString& url) { return url.startsWith("file:", Qt::CaseInsensitive) || !url.contains("://"); }

FetchResult readFile(const QString& url) {
    FetchResult r;
    QFile f(url.startsWith("file:", Qt::CaseInsensitive) ? QUrl(url).toLocalFile() : url);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = QCoreApplication::translate("eld::MainWindow", "can't read %1").arg(f.fileName());
        return r;
    }
    r.status = 200;
    r.body = f.readAll();
    return r;
}

#ifdef Q_OS_WIN
QString winError(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return QCoreApplication::translate("eld::MainWindow", "no answer in time");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return QCoreApplication::translate("eld::MainWindow", "no internet connection (the address couldn't be found)");
    case ERROR_WINHTTP_CANNOT_CONNECT: return QCoreApplication::translate("eld::MainWindow", "no connection to the server");
    case ERROR_WINHTTP_CONNECTION_ERROR: return QCoreApplication::translate("eld::MainWindow", "the connection was lost");
    case ERROR_WINHTTP_SECURE_FAILURE: return QCoreApplication::translate("eld::MainWindow", "the secure connection failed");
    default: return QCoreApplication::translate("eld::MainWindow", "Windows error %1").arg(e);
    }
}

FetchResult winHttpGet(const QString& url) {
    FetchResult r;
    const std::wstring wurl = url.toStdWString();
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = QCoreApplication::translate("eld::MainWindow", "a wrong address");
        return r;
    }
    const std::wstring agent = QString::fromUtf8(kUserAgent).toStdWString();
    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (Windows 8.1 and later) uses the proxy Windows is set to
    HINTERNET session = WinHttpOpen(agent.c_str(), 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        r.error = winError(GetLastError());
        return r;
    }
    WinHttpSetTimeouts(session, kWaitMs, kWaitMs, kWaitMs, kWaitMs);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
    if (request && WinHttpSendRequest(request, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        r.status = int(status);
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            QByteArray chunk(int(available), Qt::Uninitialized);
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            r.body += chunk.left(int(read));
            if (r.body.size() > (1 << 20)) break; // (a release's information is a few KB)
        }
    } else {
        r.error = winError(GetLastError());
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return r;
}
#endif

} // namespace

void fetchUpdateInfo(const QString& url, QObject* ctx, std::function<void(const FetchResult&)> done) {
    QPointer<QObject> guard(ctx);
    if (isFile(url)) {
        QThreadPool::globalInstance()->start([guard, done, url] { deliver(guard, done, readFile(url)); });
        return;
    }
#ifdef Q_OS_WIN
    QThreadPool::globalInstance()->start([guard, done, url] { deliver(guard, done, winHttpGet(url)); });
#else
    auto* nam = new QNetworkAccessManager(ctx);
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromUtf8(kUserAgent));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(kWaitMs);
    QNetworkReply* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, ctx, [reply, nam, done] {
        FetchResult r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        if (r.status == 0) r.error = reply->errorString();
        reply->deleteLater();
        nam->deleteLater();
        done(r);
    });
#endif
}

} // namespace eld
