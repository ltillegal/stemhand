#include "wavreader.h"

#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace stemhand {
namespace {

quint32 readLe32(const char *data)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data));
}

quint16 readLe16(const char *data)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data));
}

} // namespace

bool WavReader::open(const QString &path, QString *error)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }

    const QByteArray header = m_file.read(12);
    if (header.size() < 12 || !header.startsWith("RIFF") || header.mid(8, 4) != "WAVE") {
        if (error)
            *error = QStringLiteral("%1 is not a WAV file").arg(path);
        return false;
    }

    int bits = 0;
    qint64 dataOffset = -1;
    qint64 dataBytes = 0;

    while (!m_file.atEnd()) {
        const QByteArray chunkHeader = m_file.read(8);
        if (chunkHeader.size() < 8)
            break;
        const quint32 size = readLe32(chunkHeader.constData() + 4);
        const QByteArray id = chunkHeader.left(4);

        if (id == "fmt ") {
            const QByteArray format = m_file.read(size);
            if (format.size() < 16) {
                if (error)
                    *error = QStringLiteral("%1 has a truncated fmt chunk").arg(path);
                return false;
            }
            const quint16 audioFormat = readLe16(format.constData());
            m_channels = readLe16(format.constData() + 2);
            m_sampleRate = static_cast<int>(readLe32(format.constData() + 4));
            bits = readLe16(format.constData() + 14);
            if (audioFormat != 1 || bits != 16 || m_channels <= 0) {
                if (error) {
                    *error = QStringLiteral("%1: only 16-bit PCM is supported (format %2, %3 bit)")
                                 .arg(path)
                                 .arg(audioFormat)
                                 .arg(bits);
                }
                return false;
            }
        } else if (id == "data") {
            dataOffset = m_file.pos();
            dataBytes = size;
            m_file.seek(m_file.pos() + size);
        } else {
            m_file.seek(m_file.pos() + size);
        }

        if (size % 2 == 1 && !m_file.atEnd())
            m_file.seek(m_file.pos() + 1); // chunks are word-aligned
    }

    if (m_channels == 0 || dataOffset < 0) {
        if (error)
            *error = QStringLiteral("%1 is missing fmt or data").arg(path);
        return false;
    }

    m_frames = dataBytes / (m_channels * 2);
    m_remaining = m_frames;
    m_file.seek(dataOffset);
    return true;
}

void WavReader::close()
{
    m_file.close();
    m_channels = 0;
    m_sampleRate = 0;
    m_frames = 0;
    m_remaining = 0;
}

bool WavReader::read(qint64 frames, float *interleaved, qint64 *read, QString *error)
{
    *read = 0;
    if (!m_file.isOpen())
        return false;

    const qint64 wanted = std::min(frames, m_remaining);
    if (wanted <= 0)
        return true;

    const qint64 bytes = wanted * m_channels * 2;
    const QByteArray block = m_file.read(bytes);
    if (block.size() < bytes) {
        if (error)
            *error = QStringLiteral("short read: the WAV file is truncated");
        return false;
    }

    const auto *samples = reinterpret_cast<const qint16 *>(block.constData());
    const qint64 count = block.size() / 2;
    for (qint64 i = 0; i < count; ++i)
        interleaved[i] = samples[i] / 32768.0f;

    m_remaining -= wanted;
    *read = wanted;
    return true;
}

} // namespace stemhand
