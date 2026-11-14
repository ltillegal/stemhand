// stemhand-sign — developer tooling for the model store.
//
//   stemhand-sign keygen <private-key-file>
//   stemhand-sign sign <model-dir> <private-key-file>
//   stemhand-sign verify <model-dir> <public-key-hex-or-file>
//
// The signature covers manifest.json byte-for-byte, and the manifest pins the
// SHA-256 of every weight file, so one signature authenticates the directory.

#include "keys.h"
#include "manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

#include <sodium.h>

using namespace stemhand;

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage:\n"
                 "  stemhand-sign keygen <private-key-file>\n"
                 "  stemhand-sign sign   <model-dir> <private-key-file>\n"
                 "  stemhand-sign verify <model-dir> <public-key-hex-or-file>\n");
    return 64;
}

QByteArray readAll(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }
    *ok = true;
    return file.readAll();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    if (sodium_init() < 0)
        return 1;

    if (args.size() < 3)
        return usage();

    const QString command = args.at(1);

    if (command == QLatin1String("keygen")) {
        unsigned char publicKey[crypto_sign_PUBLICKEYBYTES];
        unsigned char secretKey[crypto_sign_SECRETKEYBYTES];
        crypto_sign_keypair(publicKey, secretKey);

        const QByteArray publicHex =
            QByteArray(reinterpret_cast<const char *>(publicKey), crypto_sign_PUBLICKEYBYTES).toHex();
        const QByteArray secretHex =
            QByteArray(reinterpret_cast<const char *>(secretKey), crypto_sign_SECRETKEYBYTES).toHex();

        QFile out(args.at(2));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(args.at(2)));
            return 1;
        }
        out.write(secretHex + "\n");
        out.close();
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        std::printf("%s\n", publicHex.constData());
        return 0;
    }

    if (command == QLatin1String("sign")) {
        if (args.size() < 4)
            return usage();

        const QString modelDir = args.at(2);
        const QString keyPath = args.at(3);

        bool ok = false;
        const QByteArray keyHex = readAll(keyPath, &ok).trimmed();
        if (!ok) {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(keyPath));
            return 1;
        }
        const QByteArray secretKey = QByteArray::fromHex(keyHex);
        if (secretKey.size() != crypto_sign_SECRETKEYBYTES) {
            std::fprintf(stderr, "private key must be %d bytes of hex\n", crypto_sign_SECRETKEYBYTES);
            return 1;
        }

        const QString manifestPath = modelDir + QStringLiteral("/manifest.json");
        const QByteArray manifest = readAll(manifestPath, &ok);
        if (!ok) {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(manifestPath));
            return 1;
        }

        QString error;
        if (!ModelManifest::parse(manifest, &error)) {
            std::fprintf(stderr, "refusing to sign an invalid manifest: %s\n", qPrintable(error));
            return 1;
        }

        unsigned char signature[crypto_sign_BYTES];
        unsigned long long signatureLength = 0;
        crypto_sign_detached(signature, &signatureLength,
                             reinterpret_cast<const unsigned char *>(manifest.constData()),
                             static_cast<unsigned long long>(manifest.size()),
                             reinterpret_cast<const unsigned char *>(secretKey.constData()));

        QFile out(modelDir + QStringLiteral("/manifest.json.sig"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::fprintf(stderr, "cannot write manifest.json.sig\n");
            return 1;
        }
        out.write(QByteArray(reinterpret_cast<const char *>(signature), crypto_sign_BYTES).toBase64()
                  + "\n");
        std::printf("signed %s\n", qPrintable(manifestPath));
        return 0;
    }

    if (command == QLatin1String("verify")) {
        if (args.size() < 4)
            return usage();

        const QString modelDir = args.at(2);
        QString keyHex = args.at(3).trimmed();
        if (QFileInfo::exists(keyHex)) {
            bool ok = false;
            keyHex = QString::fromLatin1(readAll(keyHex, &ok).trimmed());
            if (!ok) {
                std::fprintf(stderr, "cannot read %s\n", qPrintable(args.at(3)));
                return 1;
            }
        }

        bool ok = false;
        const QByteArray manifest = readAll(modelDir + QStringLiteral("/manifest.json"), &ok);
        if (!ok) {
            std::fprintf(stderr, "no manifest.json in %s\n", qPrintable(modelDir));
            return 1;
        }
        const QByteArray signature =
            QByteArray::fromBase64(readAll(modelDir + QStringLiteral("/manifest.json.sig"), &ok).trimmed());
        if (!ok) {
            std::fprintf(stderr, "no manifest.json.sig in %s\n", qPrintable(modelDir));
            return 1;
        }

        if (!verifyDetached(manifest, signature, keyHex)) {
            std::fprintf(stderr, "signature does NOT verify\n");
            return 1;
        }

        QString error;
        auto parsed = ModelManifest::parse(manifest, &error);
        if (!parsed) {
            std::fprintf(stderr, "signature ok but manifest invalid: %s\n", qPrintable(error));
            return 1;
        }
        for (const ModelFile &file : parsed->files) {
            const QString path = modelDir + QLatin1Char('/') + file.path;
            if (sha256File(path) != file.sha256) {
                std::fprintf(stderr, "weights do NOT match manifest: %s\n", qPrintable(file.path));
                return 1;
            }
        }
        std::printf("ok: %s %s, %lld files\n", qPrintable(parsed->id), qPrintable(parsed->version),
                    static_cast<long long>(parsed->files.size()));
        return 0;
    }

    return usage();
}
