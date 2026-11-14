#pragma once

#include "fft.h"

#include <QString>

#include <atomic>
#include <complex>
#include <functional>
#include <memory>
#include <vector>

namespace stemhand {

// Driver for the GTCRN speech denoiser: 16 kHz, 512-point STFT with a 256 hop,
// complex spectrum in, complex spectrum out, three recurrent caches that must
// be threaded frame to frame. The manifest's "driver" field selects this class.
class GtcrnDenoiser
{
public:
    GtcrnDenoiser();
    ~GtcrnDenoiser();

    GtcrnDenoiser(const GtcrnDenoiser &) = delete;
    GtcrnDenoiser &operator=(const GtcrnDenoiser &) = delete;

    bool load(const QString &modelPath, int threads, QString *error);
    bool process(std::vector<float> &samples, QString *error,
                 const std::function<void(int, int)> &progress = {},
                 const std::atomic_bool *cancel = nullptr);

    int sampleRate() const { return 16000; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace stemhand
