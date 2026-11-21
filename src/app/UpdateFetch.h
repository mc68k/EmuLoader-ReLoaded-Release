// UpdateFetch - asks the update check's address (UpdateCheck::url()) in the background and hands
// back the answer on the GUI thread. On Windows with WinHTTP, which is part of Windows (its own
// secure connections and proxy settings: no extra files); elsewhere with Qt Network. A file
// (file:///..., or a plain path) is read as it is.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <functional>

namespace eld {

struct FetchResult {
    int status = 0;      // the HTTP status (200, 404 ...); 0 when there was no answer (a file: 200)
    QByteArray body;
    QString error;       // why there was no answer ("" when there was one)
};

// done() is called on ctx's thread, unless ctx is gone by then. Waits 15 seconds at most.
void fetchUpdateInfo(const QString& url, QObject* ctx, std::function<void(const FetchResult&)> done);

} // namespace eld
