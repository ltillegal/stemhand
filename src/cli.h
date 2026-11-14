#pragma once

#include <QString>
#include <QUrl>

namespace stemhand {

struct DenoiseRequest {
    QString input;
    QString output;
    QString modelRef; // empty = first installed gtcrn model; else id or directory
    QString storeRoot;
    int threads = 0;  // 0 = hardware threads
};

// Headless denoise path. Returns a process exit code.
int runDenoise(const DenoiseRequest &request);

struct UpdateRequest {
    QUrl indexUrl;
    QString storeRoot;
    bool checkOnly = false;
};

// Headless update path: plan against the index, then install what is missing.
int runUpdate(const UpdateRequest &request);

// Pin or unpin the version a model id runs on, and list what is installed.
int runPrefer(const QString &reference, const QString &storeRoot);
int runRelease(const QString &id, const QString &storeRoot);
int runVersions(const QString &storeRoot);

} // namespace stemhand
