#include "stemplayer.h"

#include "wavreader.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace stemhand {

namespace {

constexpr int kMaxChunkFrames = 8192;

QString clockText(double seconds)
{
    if (seconds <= 0.0)
        return QStringLiteral("0:00");
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// Lives on the audio thread's call stack: readData() is called by QAudioSink
// and must not allocate, lock, or touch Qt objects that post events.
class StemMixDevice : public QIODevice
{
public:
    struct Params {
        std::atomic<float> gain{1.0f};
        std::atomic_bool muted{false};
        std::atomic_bool solo{false};
    };

    bool openStems(const QStringList &paths, QString *error)
    {
        m_readers.clear();
        for (const QString &path : paths) {
            auto reader = std::make_unique<WavReader>();
            if (!reader->open(path, error))
                return false;
            if (m_readers.empty()) {
                m_channels = reader->channels();
                m_rate = reader->sampleRate();
                m_frames = reader->frames();
            } else if (reader->channels() != m_channels || reader->sampleRate() != m_rate
                       || reader->frames() != m_frames) {
                if (error)
                    *error = QStringLiteral("stems disagree on format or length");
                return false;
            }
            m_readers.push_back(std::move(reader));
        }

        m_params = std::make_unique<Params[]>(static_cast<size_t>(paths.size()));
        m_count = static_cast<int>(paths.size());
        m_mix.assign(static_cast<size_t>(kMaxChunkFrames) * m_channels, 0.0f);
        m_scratch.assign(m_mix.size(), 0.0f);
        m_position = 0;
        return true;
    }

    void setStates(const QVector<StemState> &states)
    {
        const int count = std::min(m_count, static_cast<int>(states.size()));
        for (int i = 0; i < count; ++i) {
            m_params[i].gain.store(std::pow(10.0f, states.at(i).gainDb / 20.0f),
                                   std::memory_order_relaxed);
            m_params[i].muted.store(states.at(i).muted, std::memory_order_relaxed);
            m_params[i].solo.store(states.at(i).solo, std::memory_order_relaxed);
        }
    }

    qint64 readData(char *data, qint64 maxLength) override
    {
        if (m_count == 0 || m_channels == 0)
            return 0;

        const qint64 frameBytes = m_channels * 2;
        qint64 framesWanted = maxLength / frameBytes;
        framesWanted = std::min<qint64>(framesWanted, kMaxChunkFrames);
        framesWanted = std::min<qint64>(framesWanted, m_frames - m_position);
        if (framesWanted <= 0)
            return 0; // end of the mix

        bool soloActive = false;
        for (int i = 0; i < m_count; ++i)
            soloActive = soloActive || m_params[i].solo.load(std::memory_order_relaxed);

        const size_t samples = static_cast<size_t>(framesWanted) * m_channels;
        std::fill(m_mix.begin(), m_mix.begin() + static_cast<long>(samples), 0.0f);

        for (int i = 0; i < m_count; ++i) {
            const bool audible = !m_params[i].muted.load(std::memory_order_relaxed)
                && (!soloActive || m_params[i].solo.load(std::memory_order_relaxed));
            const float gain = audible ? m_params[i].gain.load(std::memory_order_relaxed) : 0.0f;

            qint64 got = 0;
            if (!m_readers[static_cast<size_t>(i)]->read(framesWanted, m_scratch.data(), &got,
                                                         nullptr))
                return -1;
            if (gain == 0.0f)
                continue;

            const size_t count = static_cast<size_t>(got) * m_channels;
            for (size_t s = 0; s < count; ++s)
                m_mix[s] += m_scratch[s] * gain;
        }

        auto *output = reinterpret_cast<qint16 *>(data);
        for (size_t s = 0; s < samples; ++s) {
            const float clamped = std::clamp(m_mix[s], -1.0f, 1.0f);
            output[s] = static_cast<qint16>(std::lround(clamped * 32767.0f));
        }

        m_position += framesWanted;
        return framesWanted * frameBytes;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

    qint64 bytesAvailable() const override
    {
        return (m_frames - m_position) * m_channels * 2 + QIODevice::bytesAvailable();
    }

    int channels() const { return m_channels; }
    int rate() const { return m_rate; }
    qint64 frames() const { return m_frames; }
    qint64 positionFrames() const { return m_position; }
    int count() const { return m_count; }

    void rewind()
    {
        for (auto &reader : m_readers)
            reader->seek(0);
        m_position = 0;
    }

private:
    std::vector<std::unique_ptr<WavReader>> m_readers;
    std::unique_ptr<Params[]> m_params;
    int m_count = 0;
    int m_channels = 0;
    int m_rate = 0;
    qint64 m_frames = 0;
    qint64 m_position = 0;
    std::vector<float> m_mix;
    std::vector<float> m_scratch;
};

struct StemPlayerPrivate
{
    StemMixDevice device;
    std::unique_ptr<QAudioSink> sink;
    QTimer positionTimer;
};

StemPlayer::StemPlayer(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<StemPlayerPrivate>())
{
    d->positionTimer.setInterval(100);
    connect(&d->positionTimer, &QTimer::timeout, this, [this] {
        const qint64 frames = d->device.positionFrames();
        const double next = d->device.rate() > 0 ? frames / static_cast<double>(d->device.rate())
                                                 : 0.0;
        if (next != m_position) {
            m_position = next;
            emit positionChanged();
        }
        if (m_position >= m_duration && m_duration > 0.0) {
            stop();
            emit finished();
        }
    });
}

StemPlayer::~StemPlayer()
{
    if (d->sink)
        d->sink->stop();
}

QString StemPlayer::positionText() const
{
    return clockText(m_position);
}

QString StemPlayer::durationText() const
{
    return clockText(m_duration);
}

void StemPlayer::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void StemPlayer::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

bool StemPlayer::load(const QStringList &paths, const QVector<StemState> &states)
{
    stop();
    setError(QString());

    if (paths.isEmpty()) {
        m_duration = 0.0;
        m_position = 0.0;
        emit loadedChanged();
        return false;
    }

    QString error;
    if (!d->device.openStems(paths, &error)) {
        setError(error);
        m_duration = 0.0;
        emit loadedChanged();
        return false;
    }
    // QAudioSink refuses a QIODevice that was never opened for reading.
    if (!d->device.isOpen() && !d->device.open(QIODevice::ReadOnly)) {
        setError(tr("Cannot open the stem stream."));
        emit loadedChanged();
        return false;
    }
    d->device.setStates(states);

    m_duration = d->device.frames() / static_cast<double>(d->device.rate());
    m_position = 0.0;

    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (output.isNull()) {
        setError(tr("No audio output device."));
        emit loadedChanged();
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(d->device.rate());
    format.setChannelCount(d->device.channels());
    format.setSampleFormat(QAudioFormat::Int16);
    if (!output.isFormatSupported(format)) {
        setError(tr("The output device does not accept %1 Hz stereo.")
                     .arg(d->device.rate()));
        emit loadedChanged();
        return false;
    }

    d->sink = std::make_unique<QAudioSink>(output, format);
    // ~200 ms of buffer: enough to ride out a slow disk read without adding
    // audible latency to a mute click.
    d->sink->setBufferSize(d->device.rate() * d->device.channels() * 2 / 5);

    emit loadedChanged();
    return true;
}

void StemPlayer::setStates(const QVector<StemState> &states)
{
    if (!d->sink && m_duration <= 0.0)
        return;
    d->device.setStates(states);
}

void StemPlayer::play()
{
    if (m_duration <= 0.0 || !d->sink)
        return;

    if (d->sink->state() == QAudio::SuspendedState) {
        d->sink->resume();
    } else if (d->sink->state() == QAudio::StoppedState) {
        d->device.rewind();
        d->sink->start(&d->device);
    }
    d->positionTimer.start();
    setPlaying(true);
}

void StemPlayer::pause()
{
    if (!d->sink)
        return;
    d->sink->suspend();
    d->positionTimer.stop();
    setPlaying(false);
}

void StemPlayer::stop()
{
    if (d->sink) {
        d->sink->stop();
        d->device.rewind();
    }
    d->positionTimer.stop();
    setPlaying(false);
    m_position = 0.0;
    emit positionChanged();
}

void StemPlayer::toggle()
{
    if (m_playing)
        pause();
    else
        play();
}

} // namespace stemhand
