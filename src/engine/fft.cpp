#include "fft.h"

#include <cmath>

namespace stemhand {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

Fft::Fft(int size)
    : m_size(size)
{
    if (m_size <= 1 || (m_size & (m_size - 1)) != 0) {
        m_size = 0;
        return;
    }

    while ((1 << m_bits) < m_size)
        ++m_bits;

    m_reverse.resize(m_size);
    for (int i = 0; i < m_size; ++i) {
        int reversed = 0;
        for (int bit = 0; bit < m_bits; ++bit)
            reversed |= ((i >> bit) & 1) << (m_bits - 1 - bit);
        m_reverse[i] = reversed;
    }

    m_twiddles.resize(m_size / 2);
    for (int i = 0; i < m_size / 2; ++i) {
        const double angle = -kTwoPi * i / m_size;
        m_twiddles[i] = {static_cast<float>(std::cos(angle)),
                         static_cast<float>(std::sin(angle))};
    }
}

void Fft::transform(std::complex<float> *data) const
{
    for (int i = 0; i < m_size; ++i) {
        if (i < m_reverse[i])
            std::swap(data[i], data[m_reverse[i]]);
    }

    for (int length = 2; length <= m_size; length <<= 1) {
        const int half = length / 2;
        const int stride = m_size / length;
        for (int start = 0; start < m_size; start += length) {
            for (int k = 0; k < half; ++k) {
                const std::complex<float> twiddle = m_twiddles[k * stride];
                const std::complex<float> even = data[start + k];
                const std::complex<float> odd = data[start + k + half] * twiddle;
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

void Fft::forward(std::complex<float> *data) const
{
    transform(data);
}

void Fft::inverse(std::complex<float> *data) const
{
    for (int i = 0; i < m_size; ++i)
        data[i] = std::conj(data[i]);
    transform(data);
    const float scale = 1.0f / static_cast<float>(m_size);
    for (int i = 0; i < m_size; ++i)
        data[i] = std::conj(data[i]) * scale;
}

} // namespace stemhand
