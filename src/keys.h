#pragma once

#include <QByteArray>
#include <QString>

namespace photone {

// SHA-256 of a file's bytes, lowercase hex. Empty string on I/O failure.
QString sha256File(const QString &path);

// True when `signature` is a valid Ed25519 signature of `message` under `publicKeyHex`.
bool verifyDetached(const QByteArray &message, const QByteArray &signature,
                    const QString &publicKeyHex);

// The public key compiled into the binary from keys/dev.pub.
QString pinnedKeyHex();

} // namespace photone
