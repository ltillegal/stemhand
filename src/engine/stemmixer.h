#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace stemhand {

struct StemState {
    float gainDb = 0.0f;
    bool muted = false;
    bool solo = false;
};

// Sums stems with per-stem gain, mute and solo.
//
// Two passes: the first measures the peak so the second can scale the whole mix
// by one factor instead of clipping samples individually. A clip is
// audible distortion; a small uniform gain is not.
class StemMixer
{
public:
    static bool render(const QStringList &stems, const QVector<StemState> &states,
                       const QString &output, QString *error, double *appliedGain = nullptr,
                       double *seconds = nullptr);
};

} // namespace stemhand
