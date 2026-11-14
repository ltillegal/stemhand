#pragma once

#include <QFile>
#include <QString>

namespace stemhand {

// Minimal streaming reader for 16-bit PCM WAV, which is what the separator
// writes. Stems are read in chunks because a four-minute song holds four of
// them at once: streaming keeps the mixer's memory flat instead of loading
// several hundred megabytes.
class WavReader
{
public:
    bool open(const QString &path, QString *error);
    void close();

    bool isOpen() const { return m_file.isOpen(); }
    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    qint64 frames() const { return m_frames; }

    // Reads up to `frames` interleaved frames; sets `read` to what it got.
    bool read(qint64 frames, float *interleaved, qint64 *read, QString *error);

private:
    QFile m_file;
    int m_channels = 0;
    int m_sampleRate = 0;
    qint64 m_frames = 0;
    qint64 m_remaining = 0;
};

} // namespace stemhand
