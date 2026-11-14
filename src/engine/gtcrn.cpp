#include "gtcrn.h"

#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

#ifdef STEMHAND_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace stemhand {

namespace {
constexpr int kFftSize = 512;
constexpr int kHop = 256;
constexpr int kBins = kFftSize / 2 + 1; // 257

constexpr int kConvCount = 2 * 1 * 16 * 16 * 33;
constexpr int kTraCount = 2 * 3 * 1 * 1 * 16;
constexpr int kInterCount = 2 * 1 * 33 * 16;
} // namespace

struct GtcrnDenoiser::Impl
{
    Impl()
    {
        window.resize(kFftSize);
        for (int i = 0; i < kFftSize; ++i) {
            // Periodic Hann (torch.hann_window(512, periodic=False) sliced to
            // 512 samples), square-rooted for analysis-synthesis WOLA.
            const double angle = 2.0 * std::numbers::pi * i / kFftSize;
            window[i] = static_cast<float>(std::sqrt(0.5 * (1.0 - std::cos(angle))));
        }
        resetCaches();
    }

    void resetCaches()
    {
        convCache.assign(kConvCount, 0.0f);
        traCache.assign(kTraCount, 0.0f);
        interCache.assign(kInterCount, 0.0f);
    }

    std::vector<float> window;
    std::vector<float> convCache;
    std::vector<float> traCache;
    std::vector<float> interCache;
    Fft fft{kFftSize};

#ifdef STEMHAND_HAVE_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "stemhand"};
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    bool loaded = false;
#else
    bool loaded = false;
#endif
};

GtcrnDenoiser::GtcrnDenoiser()
    : m_impl(std::make_unique<Impl>())
{
}

GtcrnDenoiser::~GtcrnDenoiser() = default;

bool GtcrnDenoiser::load(const QString &modelPath, int threads, QString *error)
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

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t inputCount = m_impl->session->GetInputCount();
        for (size_t i = 0; i < inputCount; ++i) {
            auto name = m_impl->session->GetInputNameAllocated(i, allocator);
            m_impl->inputNames.emplace_back(name.get());
        }
        const size_t outputCount = m_impl->session->GetOutputCount();
        for (size_t i = 0; i < outputCount; ++i) {
            auto name = m_impl->session->GetOutputNameAllocated(i, allocator);
            m_impl->outputNames.emplace_back(name.get());
        }

        m_impl->resetCaches();
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

bool GtcrnDenoiser::process(std::vector<float> &samples, QString *error,
                            const std::function<void(int, int)> &progress,
                            const std::atomic_bool *cancel)
{
#ifdef STEMHAND_HAVE_ONNXRUNTIME
    if (!m_impl->loaded) {
        if (error)
            *error = QStringLiteral("model is not loaded");
        return false;
    }

    const int length = static_cast<int>(samples.size());
    const int pad = kFftSize / 2;
    if (length <= pad) {
        if (error)
            *error = QStringLiteral("input is too short for a %1-sample window").arg(kFftSize);
        return false;
    }

    // torch.stft(center=True) pads with "reflect": the edge sample is not
    // repeated, the signal is mirrored around it.
    std::vector<float> padded(static_cast<size_t>(length) + 2 * pad);
    for (int i = 0; i < pad; ++i)
        padded[i] = samples[pad - i];
    std::memcpy(padded.data() + pad, samples.data(), static_cast<size_t>(length) * sizeof(float));
    for (int i = 0; i < pad; ++i)
        padded[pad + length + i] = samples[length - 2 - i];

    const int frames = 1 + (length + 2 * pad - kFftSize) / kHop;
    std::vector<float> accumulator(static_cast<size_t>(frames * kHop + kFftSize), 0.0f);
    std::vector<float> weights(accumulator.size(), 0.0f);

    std::vector<std::complex<float>> frame(kFftSize);
    std::vector<std::complex<float>> spectrum(kFftSize);
    std::vector<float> mix(static_cast<size_t>(kBins) * 2);
    std::vector<float> enhanced(static_cast<size_t>(kBins) * 2);

    const std::array<int64_t, 4> mixShape{1, kBins, 1, 2};
    const std::array<int64_t, 5> convShape{2, 1, 16, 16, 33};
    const std::array<int64_t, 5> traShape{2, 3, 1, 1, 16};
    const std::array<int64_t, 4> interShape{2, 1, 33, 16};

    m_impl->resetCaches();

    try {
        const Ort::MemoryInfo memory =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<const char *> inputNames;
        for (const std::string &name : m_impl->inputNames)
            inputNames.push_back(name.c_str());

        for (int index = 0; index < frames; ++index) {
            // Cancelling between frames keeps the worst-case delay under a
            // millisecond instead of the length of the file.
            if (cancel && (index % 32) == 0 && cancel->load()) {
                if (error)
                    *error = QStringLiteral("cancelled");
                return false;
            }
            if (progress && (index % 64) == 0)
                progress(index, frames);

            const float *source = padded.data() + index * kHop;
            for (int i = 0; i < kFftSize; ++i)
                frame[i] = {source[i] * m_impl->window[i], 0.0f};
            m_impl->fft.forward(frame.data());

            for (int bin = 0; bin < kBins; ++bin) {
                mix[2 * bin] = frame[bin].real();
                mix[2 * bin + 1] = frame[bin].imag();
            }

            std::vector<Ort::Value> inputs;
            inputs.push_back(Ort::Value::CreateTensor<float>(
                memory, mix.data(), mix.size(), mixShape.data(), mixShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(
                memory, m_impl->convCache.data(), m_impl->convCache.size(), convShape.data(),
                convShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(
                memory, m_impl->traCache.data(), m_impl->traCache.size(), traShape.data(),
                traShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(
                memory, m_impl->interCache.data(), m_impl->interCache.size(), interShape.data(),
                interShape.size()));

            std::vector<const char *> outputNames;
            for (const std::string &name : m_impl->outputNames)
                outputNames.push_back(name.c_str());

            std::vector<Ort::Value> outputs = m_impl->session->Run(
                Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(),
                outputNames.data(), outputNames.size());

            const float *output = outputs[0].GetTensorData<float>();
            std::copy(output, output + mix.size(), enhanced.begin());

            std::memcpy(m_impl->convCache.data(), outputs[1].GetTensorData<float>(),
                        sizeof(float) * m_impl->convCache.size());
            std::memcpy(m_impl->traCache.data(), outputs[2].GetTensorData<float>(),
                        sizeof(float) * m_impl->traCache.size());
            std::memcpy(m_impl->interCache.data(), outputs[3].GetTensorData<float>(),
                        sizeof(float) * m_impl->interCache.size());

            for (int bin = 0; bin < kBins; ++bin)
                spectrum[bin] = {enhanced[2 * bin], enhanced[2 * bin + 1]};
            // Hermitian symmetry for the real inverse transform.
            for (int bin = kBins; bin < kFftSize; ++bin)
                spectrum[bin] = std::conj(spectrum[kFftSize - bin]);

            m_impl->fft.inverse(spectrum.data());

            float *target = accumulator.data() + index * kHop;
            float *weight = weights.data() + index * kHop;
            for (int i = 0; i < kFftSize; ++i) {
                target[i] += spectrum[i].real() * m_impl->window[i];
                weight[i] += m_impl->window[i] * m_impl->window[i];
            }
        }
    } catch (const Ort::Exception &exception) {
        if (error)
            *error = QStringLiteral("ONNX Runtime: %1").arg(QString::fromUtf8(exception.what()));
        return false;
    }

    if (progress)
        progress(frames, frames);

    for (int i = 0; i < length; ++i) {
        const float normalisation = std::max(weights[pad + i], 1e-8f);
        samples[i] = accumulator[pad + i] / normalisation;
    }
    return true;
#else
    Q_UNUSED(samples)
    if (error)
        *error = QStringLiteral("this build has no ONNX Runtime support");
    return false;
#endif
}

} // namespace stemhand
