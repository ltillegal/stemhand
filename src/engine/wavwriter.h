#pragma once

#include <QFile>
#include <QString>

#include <vector>

namespace stemhand {

// Streaming 16-bit PCM WAV writer. The frame count is known up front (the
// separator processes a whole file), so the header is complete on open and
// nothing has to be patched afterwards — a crash leaves a valid but shorter
// file, never a corrupt one.
class WavWriter
{
public:
    bool open(const QString &path, int channels, int sampleRate, qint64 frames, QString *error);
    bool write(const float *interleaved, qint64 frames, QString *error);
    bool close();
    bool isOpen() const { return m_file.isOpen(); }

private:
    QFile m_file;
    int m_channels = 0;
};

} // namespace stemhand
