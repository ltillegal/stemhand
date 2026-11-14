#include "htdemucs.h"

#include "wavwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

#ifdef STEMHAND_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace stemhand {

QStringList HtdemucsSeparator::stemNames()
{
    return {QStringLiteral("drums"), QStringLiteral("bass"), QStringLiteral("other"),
            QStringLiteral("vocals")};
}

struct HtdemucsSeparator::Impl
{
    std::vector<float> weight;   // segment-length ramp window
    std::vector<float> accum;    // kStems * kChannels * kSegment
    std::vector<double> weightSum;
    bool loaded = false;

#ifdef STEMHAND_HAVE_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "stemhand"};
    std::unique_ptr<Ort::Session> session;
#endif
};

HtdemucsSeparator::HtdemucsSeparator()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->weight.assign(kSegment, 1.0f);
    m_impl->accum.assign(static_cast<size_t>(kStems) * kChannels * kSegment, 0.0f);
    m_impl->weightSum.assign(kSegment, 0.0);
}

HtdemucsSeparator::~HtdemucsSeparator() = default;

bool HtdemucsSeparator::load(const QString &modelPath, int threads, QString *error)
{
#ifdef STEMHAND_HAVE_ONNXRUNTIME
    if (!QFileInfo::exists(modelPath)) {
        if (error)
            *error = QStringLiteral("model file does not exist: %1").arg(modelPath);
        return false;
    }

    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, threads));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_impl->session = std::make_unique<Ort::Session>(
            m_impl->env, modelPath.toUtf8().constData(), options);
        m_impl->loaded = true;
        return true;
    } catch (const Ort::Exception &exception) {
        if (error)
            *error = QStringLiteral("ONNX Runtime: %1").arg(QString::fromUtf8(exception.what()));
        return false;
    }
#else
    Q_UNUSED(modelPath)
    Q_UNUSED(threads)
    if (error)
        *error = QStringLiteral("this build has no ONNX Runtime support");
    return false;
#endif
}

bool HtdemucsSeparator::separate(const QVector<float> &interleaved, const QString &outputDir,
                                 const QString &baseName, QString *error,
                                 const std::function<void(int, int)> &progress)
{
#ifdef STEMHAND_HAVE_ONNXRUNTIME
    if (!m_impl->loaded) {
        if (error)
            *error = QStringLiteral("model is not loaded");
        return false;
    }

    const qint64 frames = interleaved.size() / kChannels;
    if (frames < kSampleRate) {
        if (error)
            *error = QStringLiteral("input is shorter than a second");
        return false;
    }

    // Per-channel statistics over the whole track, as Demucs does before the
    // model and undoes after it.
    double mean[kChannels] = {0.0, 0.0};
    double variance[kChannels] = {0.0, 0.0};
    for (qint64 i = 0; i < frames; ++i) {
        for (int c = 0; c < kChannels; ++c)
            mean[c] += interleaved[i * kChannels + c];
    }
    for (int c = 0; c < kChannels; ++c)
        mean[c] /= static_cast<double>(frames);
    for (qint64 i = 0; i < frames; ++i) {
        for (int c = 0; c < kChannels; ++c) {
            const double delta = interleaved[i * kChannels + c] - mean[c];
            variance[c] += delta * delta;
        }
    }

    double deviation[kChannels];
    for (int c = 0; c < kChannels; ++c) {
        deviation[c] = std::sqrt(variance[c] / static_cast<double>(frames)) + 1e-8;
        if (deviation[c] < 1e-6)
            deviation[c] = 1e-6;
    }

    std::vector<int> starts;
    const int hop = kSegment - kOverlap;
    for (int start = 0;; start += hop) {
        starts.push_back(start);
        if (start + kSegment >= frames)
            break;
    }

    if (!QDir().mkpath(outputDir)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(outputDir);
        return false;
    }

    const QStringList names = stemNames();
    std::vector<std::unique_ptr<WavWriter>> writers;
    for (int stem = 0; stem < kStems; ++stem) {
        auto writer = std::make_unique<WavWriter>();
        const QString path = outputDir + QLatin1Char('/') + baseName + QLatin1Char('_')
            + names.at(stem) + QStringLiteral(".wav");
        if (!writer->open(path, kChannels, kSampleRate, frames, error))
            return false;
        writers.push_back(std::move(writer));
    }

    const Ort::MemoryInfo memory =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> inputShape{1, kChannels, kSegment};
    const std::array<int64_t, 4> outputShape{1, kStems, kChannels, kSegment};

    std::vector<float> chunk(static_cast<size_t>(kChannels) * kSegment);
    // Sized for a whole segment: the last flush can cover more than one hop.
    std::vector<float> flushBuffer(static_cast<size_t>(kChannels) * kSegment);
    std::fill(m_impl->accum.begin(), m_impl->accum.end(), 0.0f);
    std::fill(m_impl->weightSum.begin(), m_impl->weightSum.end(), 0.0);

    int windowStart = 0; // first sample still open for contributions

    auto flushUntil = [&](int limit) -> bool {
        const int count = std::min(limit, static_cast<int>(frames)) - windowStart;
        if (count <= 0)
            return true;

        for (int stem = 0; stem < kStems; ++stem) {
            for (int c = 0; c < kChannels; ++c) {
                const float *source =
                    m_impl->accum.data() + (static_cast<size_t>(stem) * kChannels + c) * kSegment;
                // The buffers are shifted on every flush, so index 0 is the
                // first open sample: indices are relative, not windowStart + i.
                for (int i = 0; i < count; ++i) {
                    const double weight = m_impl->weightSum[i];
                    const double normalised = weight > 1e-8 ? source[i] / weight : 0.0;
                    flushBuffer[static_cast<size_t>(i) * kChannels + c] =
                        static_cast<float>(normalised * deviation[c] + mean[c]);
                }
            }
            if (!writers[stem]->write(flushBuffer.data(), count, error))
                return false;
        }

        // Slide the open window forward.
        const int remaining = kSegment - count;
        for (int stem = 0; stem < kStems; ++stem) {
            for (int c = 0; c < kChannels; ++c) {
                float *base =
                    m_impl->accum.data() + (static_cast<size_t>(stem) * kChannels + c) * kSegment;
                std::memmove(base, base + count, sizeof(float) * static_cast<size_t>(remaining));
                std::fill(base + remaining, base + kSegment, 0.0f);
            }
        }
        std::memmove(m_impl->weightSum.data(), m_impl->weightSum.data() + count,
                     sizeof(double) * static_cast<size_t>(remaining));
        std::fill(m_impl->weightSum.begin() + remaining, m_impl->weightSum.end(), 0.0);

        windowStart += count;
        return true;
    };

    for (int index = 0; index < static_cast<int>(starts.size()); ++index) {
        const int start = starts[index];

        // Normalise this segment, zero-padding past the end of the track.
        std::fill(chunk.begin(), chunk.end(), 0.0f);
        const int available = std::min(kSegment, static_cast<int>(frames) - start);
        for (int i = 0; i < available; ++i) {
            for (int c = 0; c < kChannels; ++c) {
                const double value = interleaved[(static_cast<qint64>(start) + i) * kChannels + c];
                // The graph is [batch, channels, time]: channel-major, not
                // interleaved like the WAV it came from.
                chunk[static_cast<size_t>(c) * kSegment + i] =
                    static_cast<float>((value - mean[c]) / deviation[c]);
            }
        }

        // Crossfade ramps: only on sides that actually meet another segment.
        std::fill(m_impl->weight.begin(), m_impl->weight.end(), 1.0f);
        if (index > 0) {
            for (int i = 0; i < kOverlap; ++i)
                m_impl->weight[i] = static_cast<float>(i) / static_cast<float>(kOverlap);
        }
        if (index < static_cast<int>(starts.size()) - 1) {
            for (int i = 0; i < kOverlap; ++i) {
                m_impl->weight[kSegment - 1 - i] =
                    static_cast<float>(i) / static_cast<float>(kOverlap);
            }
        }

        if (qEnvironmentVariableIsSet("STEMHAND_DUMP_CHUNK") && index == 0) {
            QFile dump(qEnvironmentVariable("STEMHAND_DUMP_CHUNK"));
            if (dump.open(QIODevice::WriteOnly))
                dump.write(reinterpret_cast<const char *>(chunk.data()),
                           static_cast<qint64>(chunk.size() * sizeof(float)));
        }

        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<float>(
            memory, chunk.data(), chunk.size(), inputShape.data(), inputShape.size()));

        const char *inputName = "input";
        const char *outputName = "output";
        std::vector<Ort::Value> outputs;
        try {
            outputs = m_impl->session->Run(Ort::RunOptions{nullptr}, &inputName, inputs.data(), 1,
                                           &outputName, 1);
        } catch (const Ort::Exception &exception) {
            if (error)
                *error = QStringLiteral("ONNX Runtime: %1")
                             .arg(QString::fromUtf8(exception.what()));
            return false;
        }

        const float *output = outputs[0].GetTensorData<float>();
        const int offset = start - windowStart;
        const int count = std::min(kSegment, static_cast<int>(frames) - start);

        for (int stem = 0; stem < kStems; ++stem) {
            for (int c = 0; c < kChannels; ++c) {
                const float *source =
                    output + (static_cast<size_t>(stem) * kChannels + c) * kSegment;
                float *target =
                    m_impl->accum.data() + (static_cast<size_t>(stem) * kChannels + c) * kSegment;
                for (int i = 0; i < count; ++i) {
                    const int position = offset + i;
                    if (position >= kSegment)
                        break;
                    target[position] += source[i] * m_impl->weight[i];
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            const int position = offset + i;
            if (position >= kSegment)
                break;
            m_impl->weightSum[position] += m_impl->weight[i];
        }

        if (!flushUntil(start + hop))
            return false;

        if (progress)
            progress(index + 1, static_cast<int>(starts.size()));
    }

    if (!flushUntil(static_cast<int>(frames)))
        return false;

    for (auto &writer : writers)
        writer->close();
    return true;
#else
    Q_UNUSED(interleaved)
    Q_UNUSED(outputDir)
    Q_UNUSED(baseName)
    Q_UNUSED(progress)
    if (error)
        *error = QStringLiteral("this build has no ONNX Runtime support");
    return false;
#endif
}

} // namespace stemhand
