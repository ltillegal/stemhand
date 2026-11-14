#!/usr/bin/env python3
"""Reference htdemucs driver over a fixed 7.8 s segment.

The ONNX graph only accepts exactly 343980 samples (7.8 s at 44.1 kHz): the
export baked the training segment length into a Reshape and a reflect Pad, so
both shorter and longer inputs fail. Chunking is therefore mandatory, and this
file defines the framing the C++ driver must reproduce:

  normalise per channel over the whole track
  -> fixed segments, hop = segment - 25% overlap, zero pad the tail
  -> per-chunk weight ramps 0..1..0 across the overlap, sum and divide
  -> denormalise
"""

import sys
import time
import wave

import numpy as np
import onnxruntime as ort

RATE = 44100
SEGMENT = 343980          # samples, fixed by the export
OVERLAP = SEGMENT // 4    # 85995
STEMS = ["drums", "bass", "other", "vocals"]


def read_wav(path):
    with wave.open(path, "rb") as handle:
        frames = handle.readframes(handle.getnframes())
        channels = handle.getnchannels()
        rate = handle.getframerate()
    data = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    return data.reshape(-1, channels).T, rate


def write_wav(path, data, rate=RATE):
    pcm = (np.clip(data.T, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(data.shape[0])
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(pcm.tobytes())


def plan_starts(length, segment=SEGMENT, overlap=OVERLAP):
    hop = segment - overlap
    starts = []
    position = 0
    while True:
        starts.append(position)
        if position + segment >= length:
            break
        position += hop
    return starts


def chunk_weight(index, count, segment=SEGMENT, overlap=OVERLAP):
    weight = np.ones(segment, dtype=np.float32)
    ramp = np.linspace(0.0, 1.0, overlap, dtype=np.float32)
    if index > 0:
        weight[:overlap] = ramp
    if index < count - 1:
        weight[-overlap:] = ramp[::-1]
    return weight


def separate(model_path, audio, verbose=True):
    session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    channels, length = audio.shape

    # Statistics in float64: accumulating 2.6e5 squared samples in float32
    # loses about 3e-5 of relative precision, which the network then amplifies
    # to a visible difference. The C++ driver uses double, so match it.
    wide = audio.astype(np.float64)
    reference = wide.mean(axis=1, keepdims=True)
    deviation = wide.std(axis=1, keepdims=True) + 1e-8
    normalised = ((wide - reference) / deviation).astype(np.float32)

    starts = plan_starts(length)
    accumulator = np.zeros((4, channels, length), dtype=np.float64)
    weights = np.zeros(length, dtype=np.float64)

    started = time.perf_counter()
    for index, start in enumerate(starts):
        chunk = normalised[:, start : start + SEGMENT]
        if chunk.shape[1] < SEGMENT:
            chunk = np.pad(chunk, ((0, 0), (0, SEGMENT - chunk.shape[1])))
        output = session.run(None, {"input": chunk[None].astype(np.float32)})[0][0]

        end = min(start + SEGMENT, length)
        weight = chunk_weight(index, len(starts))[: end - start]
        accumulator[:, :, start:end] += output[:, :, : end - start] * weight
        weights[start:end] += weight
        if verbose:
            print(f"  chunk {index + 1}/{len(starts)} at {start / RATE:.2f}s", flush=True)
    elapsed = time.perf_counter() - started

    accumulator /= np.maximum(weights, 1e-8)
    stems = accumulator * deviation[None, :, :] + reference[None, :, :]
    return stems, elapsed


def main():
    model_path, in_path, out_prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    audio, rate = read_wav(in_path)
    assert rate == RATE, f"htdemucs wants 44.1 kHz, got {rate}"

    stems, elapsed = separate(model_path, audio)
    for index, name in enumerate(STEMS):
        write_wav(f"{out_prefix}{name}.wav", stems[index].astype(np.float32))

    duration = audio.shape[1] / RATE
    print(f"{duration:.2f}s audio in {elapsed:.2f}s ({duration / elapsed:.2f}x realtime)")


if __name__ == "__main__":
    main()
