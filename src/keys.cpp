#include "keys.h"

#include "pinned_key.h"

#include <QCryptographicHash>
#include <QFile>

#include <sodium.h>

namespace photone {

QString sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();

    return QString::fromLatin1(hash.result().toHex());
}

bool verifyDetached(const QByteArray &message, const QByteArray &signature,
                    const QString &publicKeyHex)
{
    if (signature.size() != crypto_sign_BYTES)
        return false;

    const QByteArray key = QByteArray::fromHex(publicKeyHex.toLatin1());
    if (key.size() != crypto_sign_PUBLICKEYBYTES)
        return false;

    return crypto_sign_verify_detached(
               reinterpret_cast<const unsigned char *>(signature.constData()),
               reinterpret_cast<const unsigned char *>(message.constData()),
               static_cast<unsigned long long>(message.size()),
               reinterpret_cast<const unsigned char *>(key.constData()))
        == 0;
}

QString pinnedKeyHex()
{
    return QString::fromLatin1(PHOTONE_PINNED_KEY_HEX);
}

} // namespace photone
