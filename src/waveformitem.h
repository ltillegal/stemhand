#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVector>

namespace photone {

// Draws a min/max peak envelope. Painted in C++ rather than QML Canvas so the
// per-pixel pass stays cheap while the file list and transport stay responsive.
class WaveformItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)

public:
    explicit WaveformItem(QQuickItem *parent = nullptr);

    QVariantList peaks() const { return m_peaksVariant; }
    void setPeaks(const QVariantList &peaks);

    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);

    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &gridColor);

    void paint(QPainter *painter) override;

signals:
    void peaksChanged();
    void accentChanged();
    void gridColorChanged();

private:
    QVariantList m_peaksVariant;
    QVector<qreal> m_peaks;
    QColor m_accent{0x7d, 0xd3, 0xfc};
    QColor m_gridColor{0xff, 0xff, 0xff, 0x14};
};

} // namespace photone
