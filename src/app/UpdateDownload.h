// UpdateDownload - Install Update's download: a release's zip saved to a file in the background,
// with its progress and a way to stop it. On Windows with WinHTTP (part of Windows, as for the
// update check); elsewhere with Qt Network. A file (file:///..., or a plain path, as EMULOADER_UPDATE_URL may give)
// is copied.
#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

namespace eld {

struct DownloadProgress {
    qint64 received = 0;
    qint64 total = 0; // 0 when not known
};

// progress() and done() are called on ctx's thread, unless ctx is gone by then; done("") when
// the file is saved, else why not. *stop set to true stops it (done() then says so).
void downloadUpdate(const QString& url, const QString& file, std::shared_ptr<std::atomic_bool> stop, QObject* ctx,
                    std::function<void(DownloadProgress)> progress, std::function<void(const QString& error)> done);

} // namespace eld
