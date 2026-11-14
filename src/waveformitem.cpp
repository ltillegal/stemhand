#include "waveformitem.h"

#include <QPainter>
#include <QPainterPath>

namespace photone {

WaveformItem::WaveformItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

void WaveformItem::setPeaks(const QVariantList &peaks)
{
    m_peaksVariant = peaks;
    m_peaks.clear();
    m_peaks.reserve(peaks.size());
    for (const QVariant &value : peaks)
        m_peaks.append(value.toReal());
    update();
    emit peaksChanged();
}

void WaveformItem::setAccent(const QColor &accent)
{
    if (m_accent == accent)
        return;
    m_accent = accent;
    update();
    emit accentChanged();
}

void WaveformItem::setGridColor(const QColor &gridColor)
{
    if (m_gridColor == gridColor)
        return;
    m_gridColor = gridColor;
    update();
    emit gridColorChanged();
}

void WaveformItem::paint(QPainter *painter)
{
    const QRectF bounds = boundingRect();
    if (bounds.isEmpty())
        return;

    painter->setRenderHint(QPainter::Antialiasing, false);

    // Eighth-note grid: enough structure to read timing, quiet enough to ignore.
    QPen gridPen(m_gridColor, 1.0);
    painter->setPen(gridPen);
    constexpr int divisions = 8;
    for (int i = 1; i < divisions; ++i) {
        const qreal x = bounds.left() + bounds.width() * i / divisions;
        painter->drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
    }

    const qreal middle = bounds.center().y();

    if (m_peaks.size() < 2) {
        painter->setPen(QPen(QColor(m_gridColor.red(), m_gridColor.green(),
                                    m_gridColor.blue(), 0x40),
                             1.0));
        painter->drawLine(QPointF(bounds.left(), middle), QPointF(bounds.right(), middle));
        return;
    }

    QColor baseline = m_gridColor;
    baseline.setAlpha(0x55);
    painter->setPen(QPen(baseline, 1.0));
    painter->drawLine(QPointF(bounds.left(), middle), QPointF(bounds.right(), middle));

    const int buckets = static_cast<int>(m_peaks.size() / 2);
    const qreal amplitude = bounds.height() * 0.46;
    const qreal step = bounds.width() / buckets;

    painter->setPen(QPen(m_accent, qMax<qreal>(1.0, step * 0.9)));
    for (int i = 0; i < buckets; ++i) {
        const qreal minimum = qBound(-1.0, m_peaks.at(2 * i), 1.0);
        const qreal maximum = qBound(-1.0, m_peaks.at(2 * i + 1), 1.0);
        const qreal x = bounds.left() + step * (i + 0.5);
        painter->drawLine(QPointF(x, middle - maximum * amplitude),
                          QPointF(x, middle - minimum * amplitude));
    }
}

} // namespace photone
