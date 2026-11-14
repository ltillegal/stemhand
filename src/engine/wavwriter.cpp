#include "wavwriter.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stemhand {
namespace {

void appendLe16(QByteArray &buffer, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    buffer.append(bytes, 2);
}

void appendLe32(QByteArray &buffer, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    buffer.append(bytes, 4);
}

} // namespace

bool WavWriter::open(const QString &path, int channels, int sampleRate, qint64 frames,
                     QString *error)
{
    m_channels = channels;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }

    const quint32 dataBytes = static_cast<quint32>(frames * channels * 2);
    QByteArray header;
    header.append("RIFF", 4);
    appendLe32(header, 36 + dataBytes);
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    appendLe32(header, 16);
    appendLe16(header, 1); // PCM
    appendLe16(header, static_cast<quint16>(channels));
    appendLe32(header, static_cast<quint32>(sampleRate));
    appendLe32(header, static_cast<quint32>(sampleRate * channels * 2));
    appendLe16(header, static_cast<quint16>(channels * 2));
    appendLe16(header, 16);
    header.append("data", 4);
    appendLe32(header, dataBytes);

    if (m_file.write(header) != header.size()) {
        if (error)
            *error = QStringLiteral("cannot write the WAV header to %1").arg(path);
        return false;
    }
    return true;
}

bool WavWriter::write(const float *interleaved, qint64 frames, QString *error)
{
    if (!m_file.isOpen())
        return false;

    std::vector<qint16> pcm(static_cast<size_t>(frames) * static_cast<size_t>(m_channels));
    for (qint64 i = 0; i < frames * m_channels; ++i) {
        const float clamped = std::clamp(interleaved[i], -1.0f, 1.0f);
        pcm[static_cast<size_t>(i)] = static_cast<qint16>(std::lround(clamped * 32767.0f));
    }

    const qint64 bytes = static_cast<qint64>(pcm.size() * sizeof(qint16));
    if (m_file.write(reinterpret_cast<const char *>(pcm.data()), bytes) != bytes) {
        if (error)
            *error = QStringLiteral("short write");
        return false;
    }
    return true;
}

bool WavWriter::close()
{
    m_file.close();
    return true;
}

} // namespace stemhand
