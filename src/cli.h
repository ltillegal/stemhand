#pragma once

#include <QString>

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

} // namespace photone
