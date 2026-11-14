#!/usr/bin/env python3
"""Builds a deterministic four-source test mix with ground truth.

Not music, but it has four separable sources with distinct time-frequency
behaviour, which is enough to check that a separator assigns energy to the
right stem instead of just splitting the spectrum in half.

Writes mix.wav plus drums.wav, bass.wav, other.wav, vocals.wav at 44.1 kHz
stereo, and prints the SDR of each stem against its source when given a
directory produced by the separator.
"""

import sys
import wave

import numpy as np

RATE = 44100
SECONDS = 8.0
RNG = np.random.default_rng(20261114)


def envelope(length, attack, release):
    env = np.ones(length)
    a = int(attack * RATE)
    r = int(release * RATE)
    if a > 0:
        env[:a] = np.linspace(0.0, 1.0, a)
    if r > 0:
        env[-r:] *= np.linspace(1.0, 0.0, r)
    return env


def kick(duration=0.25):
    n = int(duration * RATE)
    t = np.arange(n) / RATE
    freq = 110.0 * np.exp(-t * 18.0) + 45.0
    phase = 2 * np.pi * np.cumsum(freq) / RATE
    return np.sin(phase) * np.exp(-t * 9.0)


def hat(duration=0.05):
    n = int(duration * RATE)
    noise = RNG.standard_normal(n)
    # Cheap high-pass: difference of consecutive samples.
    noise = np.diff(noise, prepend=0.0)
    return noise * np.exp(-np.arange(n) / RATE * 60.0) * 0.25


def drums():
    track = np.zeros(int(SECONDS * RATE))
    step = int(0.5 * RATE)
    for beat in range(int(SECONDS / 0.5)):
        start = beat * step
        k = kick()
        track[start : start + len(k)] += k
        if beat % 2 == 1:
            for offset in (0.0, 0.25):
                s = start + int(offset * RATE)
                h = hat()
                track[s : s + len(h)] += h
    return track


def bass():
    track = np.zeros(int(SECONDS * RATE))
    note_len = int(0.5 * RATE)
    notes = [55.0, 55.0, 73.4, 65.4]
    for index in range(int(SECONDS / 0.5)):
        freq = notes[index % len(notes)]
        n = note_len
        t = np.arange(n) / RATE
        env = envelope(n, 0.01, 0.1)
        wave_ = np.sin(2 * np.pi * freq * t) + 0.3 * np.sin(4 * np.pi * freq * t)
        track[index * note_len : index * note_len + n] += wave_ * env * 0.6
    return track


def other():
    track = np.zeros(int(SECONDS * RATE))
    n = len(track)
    t = np.arange(n) / RATE
    env = envelope(n, 0.5, 0.8)
    for freq in (220.0, 277.2, 329.6):
        track += 0.12 * np.sin(2 * np.pi * freq * t) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.25 * t))
    return track * env


def vocals():
    track = np.zeros(int(SECONDS * RATE))
    n = len(track)
    t = np.arange(n) / RATE
    # A wobbly formant-ish tone: fundamental plus two resonances, gated in phrases.
    vibrato = 1.0 + 0.01 * np.sin(2 * np.pi * 5.5 * t)
    fundamental = 196.0 * vibrato
    phase = 2 * np.pi * np.cumsum(fundamental) / RATE
    tone = np.sin(phase) + 0.5 * np.sin(2 * phase) + 0.25 * np.sin(3 * phase)
    gate = np.zeros(n)
    for start in np.arange(0.5, SECONDS - 1.0, 2.0):
        a = int(start * RATE)
        b = int((start + 1.2) * RATE)
        gate[a:b] = envelope(b - a, 0.08, 0.2)
    return tone * gate * 0.35


def stereo(signal, pan):
    left = signal * np.sqrt(0.5 * (1.0 - pan))
    right = signal * np.sqrt(0.5 * (1.0 + pan))
    return np.stack([left, right], axis=1)


def write_wav(path, data, rate=RATE):
    pcm = (np.clip(data, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(data.shape[1])
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(pcm.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as handle:
        frames = handle.readframes(handle.getnframes())
        channels = handle.getnchannels()
    data = np.frombuffer(frames, dtype="<i2").astype(np.float64) / 32768.0
    return data.reshape(-1, channels)


def sdr(reference, estimate):
    reference = reference - reference.mean()
    estimate = estimate - estimate.mean()
    noise = estimate - reference
    return 10 * np.log10((np.sum(reference**2) + 1e-12) / (np.sum(noise**2) + 1e-12))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "score":
        stems = sys.argv[2]
        names = ["drums", "bass", "other", "vocals"]
        truth = {name: read_wav(f"build/testmix/{name}.wav") for name in names}
        print(f"{'stem':8} {'best match':12} {'SDR vs truth':>12} {'SDR vs mix':>11}")
        for name in names:
            estimate = read_wav(f"{stems}/{name}.wav")
            scores = {candidate: sdr(truth[candidate], estimate) for candidate in names}
            ranked = sorted(scores.items(), key=lambda item: -item[1])
            own = scores[name]
            vs_mix = sdr(truth[name], read_wav("build/testmix/mix.wav"))
            flag = "" if ranked[0][0] == name else f"  <-- leaks into {ranked[0][0]}"
            print(f"{name:8} {ranked[0][0]:12} {own:12.2f} {vs_mix:11.2f}{flag}")
        return

    out = "build/testmix"
    import os

    os.makedirs(out, exist_ok=True)

    sources = {
        "drums": (drums(), 0.0),
        "bass": (bass(), -0.1),
        "other": (other(), 0.25),
        "vocals": (vocals(), 0.0),
    }

    mix = np.zeros((int(SECONDS * RATE), 2))
    for name, (signal, pan) in sources.items():
        channel = stereo(signal, pan)
        write_wav(f"{out}/{name}.wav", channel)
        mix += channel

    peak = np.max(np.abs(mix))
    if peak > 0.95:
        mix *= 0.95 / peak
    write_wav(f"{out}/mix.wav", mix)
    print(f"{out}/mix.wav  {SECONDS:.0f}s  peak={np.max(np.abs(mix)):.2f}")


if __name__ == "__main__":
    main()
