#include "audiofile.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>

namespace stemhand {
namespace {

constexpr int kPreviewRate = 8000;
constexpr int kPeakBuckets = 2400;

QString toolPath(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

} // namespace

AudioFile::AudioFile(QObject *parent)
    : QObject(parent)
{
}

QString AudioFile::fileName() const
{
    if (m_path.isEmpty())
        return QString();
    return QFileInfo(m_path).fileName();
}

QString AudioFile::formatLabel() const
{
    if (m_path.isEmpty())
        return QString();

    QString label = QFileInfo(m_path).suffix().toUpper();
    if (label.isEmpty())
        label = QStringLiteral("AUDIO");
    if (m_sampleRate > 0) {
        label += QStringLiteral(" · %1 kHz").arg(m_sampleRate / 1000.0, 0, 'f',
                                                 m_sampleRate % 1000 == 0 ? 0 : 1);
    }
    if (m_channels > 0)
        label += QStringLiteral(" · %1").arg(m_channels == 1 ? QStringLiteral("mono")
                                                             : QStringLiteral("stereo"));
    return label;
}

void AudioFile::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void AudioFile::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void AudioFile::clear()
{
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_buffer.clear();
    m_path.clear();
    m_duration = 0.0;
    m_sampleRate = 0;
    m_channels = 0;
    m_codec.clear();
    m_peaks.clear();
    setError(QString());
    setLoading(false);
    emit fileChanged();
    emit metadataChanged();
    emit peaksChanged();
}

void AudioFile::load(const QUrl &url)
{
    const QString local = url.toLocalFile();
    if (local.isEmpty()) {
        setError(QStringLiteral("that is not a local file"));
        return;
    }
    if (!QFileInfo::exists(local)) {
        setError(QStringLiteral("no such file: %1").arg(local));
        return;
    }

    clear();
    m_path = local;
    emit fileChanged();

    if (toolPath(QStringLiteral("ffmpeg")).isEmpty()
        || toolPath(QStringLiteral("ffprobe")).isEmpty()) {
        setError(QStringLiteral("ffmpeg and ffprobe are required but were not found in PATH"));
        return;
    }

    probe(local);
    startDecode(local);
}

void AudioFile::probe(const QString &path)
{
    QProcess probe;
    probe.start(toolPath(QStringLiteral("ffprobe")),
                {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                 QStringLiteral("format=duration,format_name:stream=sample_rate,channels,codec_name"),
                 QStringLiteral("-of"), QStringLiteral("json"), path});
    if (!probe.waitForFinished(8000)) {
        setError(QStringLiteral("ffprobe timed out"));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    m_duration = format.value(QStringLiteral("duration")).toString().toDouble();
    m_codec = format.value(QStringLiteral("format_name")).toString();

    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        m_sampleRate = stream.value(QStringLiteral("sample_rate")).toString().toInt();
        m_channels = stream.value(QStringLiteral("channels")).toInt();
        if (m_sampleRate > 0)
            break;
    }

    emit metadataChanged();
}

void AudioFile::startDecode(const QString &path)
{
    m_buffer.clear();
    setLoading(true);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer.append(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::finished, this, [this] { finishDecode(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setLoading(false);
        setError(QStringLiteral("ffmpeg failed to start"));
    });

    m_process->start(toolPath(QStringLiteral("ffmpeg")),
                     {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
                      QStringLiteral("-map"), QStringLiteral("a:0"), QStringLiteral("-ac"),
                      QStringLiteral("1"), QStringLiteral("-ar"), QString::number(kPreviewRate),
                      QStringLiteral("-f"), QStringLiteral("f32le"), QStringLiteral("-")});
}

void AudioFile::finishDecode()
{
    if (!m_process)
        return;

    m_buffer.append(m_process->readAllStandardOutput());
    const QByteArray stderrOutput = m_process->readAllStandardError();
    const int exitCode = m_process->exitCode();
    m_process->deleteLater();
    m_process = nullptr;

    setLoading(false);

    if (exitCode != 0) {
        setError(QStringLiteral("ffmpeg could not decode this file: %1")
                     .arg(QString::fromUtf8(stderrOutput).trimmed().left(300)));
        return;
    }

    const auto *samples = reinterpret_cast<const float *>(m_buffer.constData());
    const qsizetype count = m_buffer.size() / static_cast<qsizetype>(sizeof(float));
    if (count <= 0) {
        setError(QStringLiteral("no audio samples decoded"));
        return;
    }

    const int buckets = qMin<int>(kPeakBuckets, static_cast<int>(count));
    QVector<QPair<float, float>> envelope(buckets, {0.0f, 0.0f});

    float peak = 0.0f;
    for (qsizetype i = 0; i < count; ++i) {
        const float sample = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        const int bucket = static_cast<int>((i * buckets) / count);
        QPair<float, float> &slot = envelope[bucket];
        slot.first = qMin(slot.first, sample);
        slot.second = qMax(slot.second, sample);
        peak = qMax(peak, std::fabs(sample));
    }

    // A quiet take should still look like a waveform, but leave the headroom
    // so a normalised master does not clip at the panel edge.
    const float scale = peak > 0.001f ? (0.96f / peak) : 0.0f;

    m_peaks.clear();
    m_peaks.reserve(buckets * 2);
    for (const auto &slot : envelope) {
        m_peaks.append(static_cast<double>(slot.first * scale));
        m_peaks.append(static_cast<double>(slot.second * scale));
    }

    emit peaksChanged();
}

} // namespace stemhand
