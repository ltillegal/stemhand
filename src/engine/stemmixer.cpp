#include "stemmixer.h"

#include "wavreader.h"
#include "wavwriter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stemhand {
namespace {

constexpr qint64 kChunkFrames = 8192;

float linearGain(float decibels)
{
    return std::pow(10.0f, decibels / 20.0f);
}

bool anySolo(const QVector<StemState> &states)
{
    for (const StemState &state : states) {
        if (state.solo)
            return true;
    }
    return false;
}

bool audible(const StemState &state, bool soloActive)
{
    if (state.muted)
        return false;
    return !soloActive || state.solo;
}

} // namespace

bool StemMixer::render(const QStringList &stems, const QVector<StemState> &states,
                       const QString &output, QString *error, double *appliedGain,
                       double *seconds)
{
    if (stems.isEmpty() || stems.size() != states.size()) {
        if (error)
            *error = QStringLiteral("the stem list and its settings do not match");
        return false;
    }

    std::vector<WavReader> readers(stems.size());
    for (size_t i = 0; i < stems.size(); ++i) {
        if (!readers[i].open(stems.at(static_cast<int>(i)), error))
            return false;
        if (readers[i].channels() != readers[0].channels()
            || readers[i].sampleRate() != readers[0].sampleRate()) {
            if (error)
                *error = QStringLiteral("stems disagree on format: %1")
                             .arg(stems.at(static_cast<int>(i)));
            return false;
        }
    }

    const int channels = readers[0].channels();
    const int rate = readers[0].sampleRate();
    const qint64 frames = readers[0].frames();
    for (const WavReader &reader : readers) {
        if (reader.frames() != frames) {
            if (error)
                *error = QStringLiteral("stems disagree on length");
            return false;
        }
    }

    const bool soloActive = anySolo(states);
    std::vector<float> gain(states.size(), 0.0f);
    for (size_t i = 0; i < states.size(); ++i) {
        const bool on = audible(states[i], soloActive);
        gain[i] = on ? linearGain(states[i].gainDb) : 0.0f;
    }

    std::vector<float> chunk(static_cast<size_t>(kChunkFrames) * channels);
    std::vector<float> stemChunk(chunk.size());

    // Pass one: how loud does the mix actually get?
    float peak = 0.0f;
    for (qint64 position = 0; position < frames;) {
        const qint64 wanted = std::min(kChunkFrames, frames - position);
        std::fill(chunk.begin(), chunk.begin() + wanted * channels, 0.0f);

        for (size_t i = 0; i < readers.size(); ++i) {
            qint64 got = 0;
            if (!readers[i].read(wanted, stemChunk.data(), &got, error))
                return false;
            if (gain[i] == 0.0f)
                continue;
            const size_t count = static_cast<size_t>(got) * channels;
            for (size_t s = 0; s < count; ++s)
                chunk[s] += stemChunk[s] * gain[i];
        }

        for (size_t s = 0; s < static_cast<size_t>(wanted) * channels; ++s)
            peak = std::max(peak, std::fabs(chunk[s]));
        position += wanted;
    }

    const double scale = peak > 0.99f ? 0.99 / peak : 1.0;
    if (appliedGain)
        *appliedGain = scale;

    // Pass two: write it.
    for (size_t i = 0; i < readers.size(); ++i) {
        if (!readers[i].open(stems.at(static_cast<int>(i)), error))
            return false;
    }

    WavWriter writer;
    if (!writer.open(output, channels, rate, frames, error))
        return false;

    for (qint64 position = 0; position < frames;) {
        const qint64 wanted = std::min(kChunkFrames, frames - position);
        std::fill(chunk.begin(), chunk.begin() + wanted * channels, 0.0f);

        for (size_t i = 0; i < readers.size(); ++i) {
            qint64 got = 0;
            if (!readers[i].read(wanted, stemChunk.data(), &got, error))
                return false;
            if (gain[i] == 0.0f)
                continue;
            const size_t count = static_cast<size_t>(got) * channels;
            for (size_t s = 0; s < count; ++s)
                chunk[s] += stemChunk[s] * gain[i];
        }

        for (size_t s = 0; s < static_cast<size_t>(wanted) * channels; ++s)
            chunk[s] = static_cast<float>(chunk[s] * scale);

        if (!writer.write(chunk.data(), wanted, error))
            return false;
        position += wanted;
    }

    writer.close();
    if (seconds)
        *seconds = frames / static_cast<double>(rate);
    return true;
}

} // namespace stemhand
