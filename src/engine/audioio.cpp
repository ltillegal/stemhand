#include "audioio.h"

#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>
#include <cmath>

namespace stemhand {

bool decodeToMono(const QString &path, int sampleRate, QVector<float> *samples, QString *error)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QStringLiteral("ffmpeg is not in PATH");
        return false;
    }

    QProcess process;
    process.start(ffmpeg,
                  {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
                   QStringLiteral("-map"), QStringLiteral("a:0"), QStringLiteral("-ac"),
                   QStringLiteral("1"), QStringLiteral("-ar"), QString::number(sampleRate),
                   QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-")});

    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("ffmpeg did not start");
        return false;
    }

    QByteArray buffer;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        buffer.append(process.readAllStandardOutput());
    }
    buffer.append(process.readAllStandardOutput());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("ffmpeg could not decode the input: %1")
                         .arg(QString::fromUtf8(process.readAllStandardError()).trimmed().left(300));
        }
        return false;
    }

    const qsizetype count = buffer.size() / static_cast<qsizetype>(sizeof(float));
    samples->resize(count);
    std::memcpy(samples->data(), buffer.constData(), static_cast<size_t>(count) * sizeof(float));

    if (count == 0) {
        if (error)
            *error = QStringLiteral("no audio decoded");
        return false;
    }
    return true;
}

bool writeWav(const QString &path, const QVector<float> &samples, int sampleRate, QString *error)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = QStringLiteral("ffmpeg is not in PATH");
        return false;
    }

    QProcess process;
    process.start(ffmpeg,
                  {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                   QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-ar"),
                   QString::number(sampleRate), QStringLiteral("-ac"), QStringLiteral("1"),
                   QStringLiteral("-i"), QStringLiteral("-"), QStringLiteral("-c:a"),
                   QStringLiteral("pcm_s16le"), path});

    if (!process.waitForStarted(5000)) {
        if (error)
            *error = QStringLiteral("ffmpeg did not start");
        return false;
    }

    process.write(reinterpret_cast<const char *>(samples.constData()),
                  static_cast<qint64>(samples.size() * sizeof(float)));
    process.closeWriteChannel();

    if (!process.waitForFinished(60000)) {
        if (error)
            *error = QStringLiteral("ffmpeg timed out writing the output");
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("ffmpeg could not write the output: %1")
                         .arg(QString::fromUtf8(process.readAllStandardError()).trimmed().left(300));
        return false;
    }
    return true;
}

float preventClipping(QVector<float> *samples, float ceiling)
{
    float peak = 0.0f;
    for (const float sample : *samples)
        peak = std::max(peak, std::fabs(sample));

    if (peak <= ceiling || peak <= 0.0f)
        return 1.0f;

    const float gain = ceiling / peak;
    for (float &sample : *samples)
        sample *= gain;
    return gain;
}

} // namespace stemhand
