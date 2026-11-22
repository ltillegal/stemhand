#pragma once

#include "engine/batchrunner.h"

#include <QString>
#include <QStringList>
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

struct SeparateRequest {
    QString input;
    QString outputDir;
    QString modelRef;
    QString storeRoot;
    int threads = 0;
};

// Headless four-stem separation path.
int runSeparate(const SeparateRequest &request);

struct BatchRequest {
    BatchOperation operation = BatchOperation::Denoise;
    QStringList inputs;
    QString outputDir;
    QString modelRef;
    QString storeRoot;
    int threads = 0;
};

// Applies one operation to many files, sharing the model between them.
int runBatch(const BatchRequest &request);

struct MixRequest {
    QString stemsDir;
    QString output;
    QStringList muted;
    QStringList soloed;
    QList<QPair<QString, double>> gains;
};

// Renders a mix from a folder of stems, honouring mute/solo/gain.
int runMix(const MixRequest &request);

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
