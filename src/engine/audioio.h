#pragma once

#include <QString>
#include <QVector>

namespace stemhand {

// Locates ffmpeg/ffprobe. PATH first, then the places Homebrew and distros put
// them: an .app launched from Finder gets a minimal PATH and would otherwise
// report that ffmpeg is missing on a machine where it is installed.
QString findTool(const QString &name);

// Blocking ffmpeg I/O for the CLI path. The GUI will get a streaming variant
// later; the trade here is one code path that behaves the same everywhere.
bool decodeToChannels(const QString &path, int sampleRate, int channels,
                      QVector<float> *interleaved, QString *error);
bool decodeToMono(const QString &path, int sampleRate, QVector<float> *samples, QString *error);

bool writeWav(const QString &path, const QVector<float> &samples, int sampleRate, QString *error);

// Peak-normalises in place only if the signal would clip; returns the gain used.
float preventClipping(QVector<float> *samples, float ceiling = 0.999f);

} // namespace stemhand
