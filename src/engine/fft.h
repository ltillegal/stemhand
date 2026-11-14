#pragma once

#include <complex>
#include <vector>

namespace stemhand {

// Radix-2 FFT for the 512-point STFT GTCRN expects.
//
// Deliberately hand-rolled and small: the engine needs one transform size now,
// and a dependency-free plan keeps the macOS build to Qt + libsodium + ORT.
// DFN3's 960-point transform is not a power of two; when that lands this gets
// replaced by a mixed-radix implementation rather than extended.
class Fft
{
public:
    explicit Fft(int size);

    int size() const { return m_size; }
    bool isValid() const { return m_size > 1; }

    void forward(std::complex<float> *data) const;
    void inverse(std::complex<float> *data) const;

private:
    void transform(std::complex<float> *data) const;

    int m_size = 0;
    int m_bits = 0;
    std::vector<int> m_reverse;
    std::vector<std::complex<float>> m_twiddles;
};

} // namespace stemhand
