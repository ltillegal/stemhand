#pragma once

#include <QString>
#include <QStringList>

namespace photone {

// Dotted numeric versions, compared field by field so that "1.10.0" sorts after
// "1.9.0". Not full semver: pre-release tags are not used by the model store.
inline bool versionLess(const QString &left, const QString &right)
{
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const qsizetype count = qMax(a.size(), b.size());
    for (qsizetype i = 0; i < count; ++i) {
        const int x = i < a.size() ? a.at(i).toInt() : 0;
        const int y = i < b.size() ? b.at(i).toInt() : 0;
        if (x != y)
            return x < y;
    }
    return false;
}

inline bool versionEqual(const QString &left, const QString &right)
{
    return !versionLess(left, right) && !versionLess(right, left);
}

} // namespace photone
