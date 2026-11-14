#pragma once

#include <QString>
#include <QUrl>

namespace photone {

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

} // namespace photone
