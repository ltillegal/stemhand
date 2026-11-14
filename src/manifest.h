#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace photone {

struct ModelFile {
    QString path;   // relative to the model version directory, no traversal
    QString sha256; // lowercase hex
    qint64 size = -1;
};

// The manifest is the trust anchor of the model store.
//
// The Ed25519 signature covers the manifest bytes exactly as stored, and the
// manifest pins the SHA-256 of every weight file. Verifying one signature
// therefore authenticates the whole model directory transitively, and a
// tampered weight file is caught without a signature per file.
struct ModelManifest {
    static constexpr int kSchema = 1;

    int schema = 0;
    QString id;      // stable, e.g. "denoise/deepfilternet3"
    QString version; // semver, e.g. "3.0.0"
    QString name;    // display name
    QString kind;    // music4 | drums5 | voice | denoise | enhance
    int engineAbi = 0;
    int sampleRate = 0;
    int channels = 0;
    QStringList stems;
    QStringList eps;
    QString minEngine;
    QString codeLicense;
    QString weightsLicense;
    bool commercialOk = false;
    QString homepage;
    QList<ModelFile> files;

    static std::optional<ModelManifest> parse(const QByteArray &json, QString *error);
};

} // namespace photone
