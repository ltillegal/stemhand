#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace stemhand {

// htdemucs (Hybrid Transformer Demucs) driver.
//
// The exported graph accepts exactly 343980 samples — 7.8 s at 44.1 kHz — and
// nothing else: the export baked the training segment into a Reshape and a
// reflect Pad, so shorter and longer inputs both fail at runtime. Separation is
// therefore chunked, with a quarter-segment overlap and linear crossfades, and
// stems are streamed to disk so memory stays bounded by one segment rather than
// by the length of the track.
class HtdemucsSeparator
{
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kSegment = 343980;
    static constexpr int kOverlap = kSegment / 4;
    static constexpr int kStems = 4;
    static constexpr int kChannels = 2;

    static QStringList stemNames();

    HtdemucsSeparator();
    ~HtdemucsSeparator();

    HtdemucsSeparator(const HtdemucsSeparator &) = delete;
    HtdemucsSeparator &operator=(const HtdemucsSeparator &) = delete;

    bool load(const QString &modelPath, int threads, QString *error);

    // `interleaved` is 44.1 kHz stereo. Writes <base>_drums.wav and friends into
    // `outputDir`; `progress` is called with (chunkIndex, chunkCount).
    // `cancel` is polled between chunks; a cancelled run removes the stems it
    // has already written, so a half-separated folder never looks finished.
    bool separate(const QVector<float> &interleaved, const QString &outputDir,
                  const QString &baseName, QString *error,
                  const std::function<void(int, int)> &progress = {},
                  const std::atomic_bool *cancel = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace stemhand
