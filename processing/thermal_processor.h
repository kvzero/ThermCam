#ifndef THERMAL_PROCESSOR_H
#define THERMAL_PROCESSOR_H

#include <QObject>
#include <QSize>
#include <QReadWriteLock>

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

    /* --- Runtime Controls --- */
    void setTargetSize(const QSize& size);
    void setPalette(ThermalPalette::Id id);
    ThermalPalette::Id palette() const { return m_palette; }

    /**
     * @brief Renders one palette preview from the latest cached grayscale frame.
     * @param id Target palette.
     * @param size Output preview size in pixels.
     * @return Colorized preview image. Null image if no source frame is available.
     */
    QImage renderPreview(ThermalPalette::Id id, const QSize& size) const;

public slots:
    void processFrame(const RawFrame& raw);

signals:
    void frameReady(const VisualFrame& frame);

private:
    struct LatestGrayFrame {
        QByteArray pixelData;
        int w = 0;
        int h = 0;
        int strideBytes = 0;
    };

    bool isFrameUsable(const RawFrame& raw) const;
    void cacheLatestGrayFrame(const RawFrame& raw);
    QImage colorizeGrayFrame(const RawFrame& raw) const;
    QImage colorizeGrayImage(const QImage& grayImage, ThermalPalette::Id id) const;
    TempPt mapPointToTarget(const TempPt& source, int srcW, int srcH) const;

    mutable QReadWriteLock m_grayLock;
    LatestGrayFrame m_latestGray;
    QSize m_targetSize;
    ThermalPalette::Id m_palette = ThermalPalette::Id::Spectra;
};

#endif // THERMAL_PROCESSOR_H
