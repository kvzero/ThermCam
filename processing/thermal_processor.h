#ifndef THERMAL_PROCESSOR_H
#define THERMAL_PROCESSOR_H

#include <QObject>
#include <QSize>

#include "core/types.h"
#include "processing/thermal_palette.h"

/**
 * @brief Thermal pseudo-color renderer for raw grayscale frames.
 *
 * Processing pipeline:
 * - Input: RawFrame (Gray8)
 * - Colorization: ThermalPalette LUT (ARGB32)
 * - Scaling: RGA hardware acceleration
 * - Output: VisualFrame for UI and capture service
 */
class ThermalProcessor : public QObject {
    Q_OBJECT
public:
    explicit ThermalProcessor(QObject *parent = nullptr);

    void setTargetSize(const QSize& size);
    void setPalette(ThermalPalette::Id id);
    ThermalPalette::Id palette() const { return m_palette; }

public slots:
    void processFrame(const RawFrame& raw);

signals:
    void frameReady(const VisualFrame& frame);

private:
    bool isFrameUsable(const RawFrame& raw) const;
    QImage colorizeGrayFrame(const RawFrame& raw) const;
    TempPt mapPointToTarget(const TempPt& source, int srcW, int srcH) const;

    QSize m_targetSize;
    ThermalPalette::Id m_palette = ThermalPalette::Id::Spectra;
};

#endif // THERMAL_PROCESSOR_H
