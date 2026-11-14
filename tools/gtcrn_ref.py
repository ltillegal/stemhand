#!/usr/bin/env python3
"""Reference GTCRN driver.

Implements the streaming loop in numpy/onnxruntime so the C++ engine can be
checked against it. This file is a test oracle, not part of the app.

STFT: 16 kHz, n_fft=512, hop=256, sqrt-Hann window, center=True (reflect).
The network sees one complex frame at a time plus three recurrent caches.
"""

import sys
import time
import wave

import numpy as np
import onnxruntime as ort

N_FFT = 512
HOP = 256
WIN = np.sqrt(np.hanning(N_FFT + 1)[:-1])  # periodic=False Hann, then sqrt


def read_wav(path):
    with wave.open(path, "rb") as handle:
        assert handle.getsampwidth() == 2, "expected 16-bit PCM"
        frames = handle.readframes(handle.getnframes())
        rate = handle.getframerate()
    data = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    return data, rate


def write_wav(path, data, rate):
    clipped = np.clip(data, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(pcm.tobytes())


def stft(signal, n_fft=N_FFT, hop=HOP, window=WIN):
    pad = n_fft // 2
    padded = np.pad(signal, (pad, pad), mode="reflect")
    count = 1 + (len(padded) - n_fft) // hop
    frames = np.stack([padded[i * hop : i * hop + n_fft] for i in range(count)])
    return np.fft.rfft(frames * window, n=n_fft, axis=1).T  # [freq, frames]


def istft(spec, length, n_fft=N_FFT, hop=HOP, window=WIN):
    frames = np.fft.irfft(spec.T, n=n_fft, axis=1)
    out = np.zeros(hop * (len(frames) - 1) + n_fft, dtype=np.float64)
    weight = np.zeros_like(out)
    for index, frame in enumerate(frames):
        start = index * hop
        out[start : start + n_fft] += frame * window
        weight[start : start + n_fft] += window**2
    out /= np.maximum(weight, 1e-8)
    return out[n_fft // 2 : n_fft // 2 + length].astype(np.float32)


def si_snr(reference, estimate):
    reference = reference - reference.mean()
    estimate = estimate - estimate.mean()
    scale = np.dot(reference, estimate) / (np.dot(reference, reference) + 1e-9)
    target = scale * reference
    noise = estimate - target
    return 10 * np.log10((np.sum(target**2) + 1e-9) / (np.sum(noise**2) + 1e-9))


def main():
    model_path = sys.argv[1]
    in_path = sys.argv[2]
    out_path = sys.argv[3]
    reference_path = sys.argv[4] if len(sys.argv) > 4 else None

    signal, rate = read_wav(in_path)
    assert rate == 16000, f"GTCRN wants 16 kHz, got {rate}"

    session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    inputs = {spec.name: spec for spec in session.get_inputs()}
    caches = {
        name: np.zeros(spec.shape, dtype=np.float32)
        for name, spec in inputs.items()
        if name != "mix"
    }

    spectrum = stft(signal)
    frames = spectrum.shape[1]
    enhanced = np.zeros_like(spectrum)

    started = time.perf_counter()
    for index in range(frames):
        mix = np.stack([spectrum[:, index].real, spectrum[:, index].imag], axis=-1)
        mix = mix[None, :, None, :].astype(np.float32)
        feeds = {"mix": mix, **caches}
        outputs = session.run(None, feeds)
        result = outputs[0][0, :, 0, :]
        enhanced[:, index] = result[:, 0] + 1j * result[:, 1]
        for name in caches:
            caches[name] = outputs[1 + list(caches).index(name)]
    elapsed = time.perf_counter() - started

    audio = istft(enhanced, len(signal))
    write_wav(out_path, audio, rate)

    duration = len(signal) / rate
    print(f"frames={frames} elapsed={elapsed:.3f}s audio={duration:.3f}s "
          f"realtime_factor={duration / elapsed:.2f}x")

    if reference_path:
        reference, _ = read_wav(reference_path)
        length = min(len(reference), len(audio))
        corr = np.corrcoef(reference[:length], audio[:length])[0, 1]
        print(f"vs reference: si_snr={si_snr(reference[:length], audio[:length]):.2f} dB "
              f"correlation={corr:.4f}")


if __name__ == "__main__":
    main()
