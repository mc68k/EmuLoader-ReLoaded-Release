#include "UpdateDownload.h"

#include <QCoreApplication>
#include <QElapsedTimer>
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
#endif

namespace eld {

namespace {

const char* kUserAgent = EL_PRODUCT_NAME " " EL_VERSION_LABEL;
constexpr int kWaitMs = 30000; // for each step (connecting, each piece received)

QString stopped() { return QCoreApplication::translate("eld::MainWindow", "the download was stopped"); }

struct Reporter {
    QPointer<QObject> ctx;
    std::function<void(DownloadProgress)> progress;
    std::function<void(const QString&)> done;
    QElapsedTimer since;
    void report(qint64 received, qint64 total, bool now = false) {
        if (!now && since.isValid() && since.elapsed() < 100) return; // (10 times a second at most)
        since.restart();
        if (!ctx) return;
        auto p = progress;
        auto c = ctx;
        QMetaObject::invokeMethod(ctx, [c, p, received, total] {
            if (c && p) p({received, total});
        }, Qt::QueuedConnection);
    }
    void finish(const QString& error) {
        if (!ctx) return;
        auto d = done;
        auto c = ctx;
        QMetaObject::invokeMethod(ctx, [c, d, error] {
            if (c) d(error);
        }, Qt::QueuedConnection);
    }
};

bool isFile(const QString& url) { return url.startsWith("file:", Qt::CaseInsensitive) || !url.contains("://"); }

QString copyFile(const QString& url, const QString& file, const std::shared_ptr<std::atomic_bool>& stop, Reporter& rep) {
    QFile in(url.startsWith("file:", Qt::CaseInsensitive) ? QUrl(url).toLocalFile() : url);
    if (!in.open(QIODevice::ReadOnly)) return QCoreApplication::translate("eld::MainWindow", "can't read %1").arg(in.fileName());
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return QCoreApplication::translate("eld::MainWindow", "%1 can't be written").arg(file);
    const qint64 total = in.size();
    qint64 got = 0;
    while (!in.atEnd()) {
        if (*stop) return stopped();
        const QByteArray chunk = in.read(1 << 20);
        if (out.write(chunk) != chunk.size()) return QCoreApplication::translate("eld::MainWindow", "%1 can't be written").arg(file);
        got += chunk.size();
        rep.report(got, total);
    }
    rep.report(got, total, true);
    return {};
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

QString winHttpDownload(const QString& url, const QString& file, const std::shared_ptr<std::atomic_bool>& stop, Reporter& rep) {
    const std::wstring wurl = url.toStdWString();
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4095;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return QCoreApplication::translate("eld::MainWindow", "a wrong address");
    const std::wstring agent = QString::fromUtf8(kUserAgent).toStdWString();
    HINTERNET session = WinHttpOpen(agent.c_str(), 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY */, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return winError(GetLastError());
    WinHttpSetTimeouts(session, kWaitMs, kWaitMs, kWaitMs, kWaitMs);
    QString error;
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    // (GitHub sends the file from another address: WinHTTP follows it)
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        wchar_t length[32] = {};
        DWORD lsize = sizeof length;
        qint64 total = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length, &lsize, WINHTTP_NO_HEADER_INDEX))
            total = QString::fromWCharArray(length).toLongLong();
        if (status != 200) {
            error = QCoreApplication::translate("eld::MainWindow", "GitHub answered with an error (%1)").arg(status);
        } else {
            QFile out(file);
            if (!out.open(QIODevice::WriteOnly)) {
                error = QCoreApplication::translate("eld::MainWindow", "%1 can't be written").arg(file);
            } else {
                qint64 got = 0;
                QByteArray chunk(1 << 16, Qt::Uninitialized);
                for (;;) {
                    if (*stop) {
                        error = stopped();
                        break;
                    }
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), DWORD(chunk.size()), &read)) {
                        error = winError(GetLastError());
                        break;
                    }
                    if (read == 0) break; // all of it
                    if (out.write(chunk.constData(), read) != qint64(read)) {
                        error = QCoreApplication::translate("eld::MainWindow", "%1 can't be written").arg(file);
                        break;
                    }
                    got += read;
                    rep.report(got, total);
                }
                rep.report(got, total, true);
            }
        }
    } else {
        error = winError(GetLastError());
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return error;
}
#endif

} // namespace

void downloadUpdate(const QString& url, const QString& file, std::shared_ptr<std::atomic_bool> stop, QObject* ctx,
                    std::function<void(DownloadProgress)> progress, std::function<void(const QString& error)> done) {
    auto rep = std::make_shared<Reporter>();
    rep->ctx = ctx;
    rep->progress = std::move(progress);
    rep->done = std::move(done);
    if (isFile(url)) {
        QThreadPool::globalInstance()->start([url, file, stop, rep] { rep->finish(copyFile(url, file, stop, *rep)); });
        return;
    }
#ifdef Q_OS_WIN
    QThreadPool::globalInstance()->start([url, file, stop, rep] { rep->finish(winHttpDownload(url, file, stop, *rep)); });
#else
    auto* nam = new QNetworkAccessManager(ctx);
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromUtf8(kUserAgent));
    req.setTransferTimeout(kWaitMs);
    auto out = std::make_shared<QFile>(file);
    if (!out->open(QIODevice::WriteOnly)) {
        rep->finish(QCoreApplication::translate("eld::MainWindow", "%1 can't be written").arg(file));
        nam->deleteLater();
        return;
    }
    QNetworkReply* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::readyRead, ctx, [reply, out, stop] {
        if (*stop) reply->abort();
        else out->write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, ctx, [rep, stop, reply](qint64 got, qint64 total) {
        if (*stop) reply->abort();
        rep->report(got, total > 0 ? total : 0);
    });
    QObject::connect(reply, &QNetworkReply::finished, ctx, [reply, nam, out, rep, stop] {
        out->write(reply->readAll());
        out->close();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        if (*stop) error = stopped();
        else if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (status != 200) error = QCoreApplication::translate("eld::MainWindow", "GitHub answered with an error (%1)").arg(status);
        reply->deleteLater();
        nam->deleteLater();
        rep->finish(error);
    });
#endif
}

} // namespace eld
