#include "thermal_processor.h"

#include "hardware/rga/rga_image.h"

#include <QtGlobal>

ThermalProcessor::ThermalProcessor(QObject *parent) : QObject(parent) {}

void ThermalProcessor::setTargetSize(const QSize& size) {
    m_targetSize = size;
}

void ThermalProcessor::setPalette(ThermalPalette::Id id) {
    if (id == ThermalPalette::Id::Count) return;
    m_palette = id;
}

void ThermalProcessor::processFrame(const RawFrame& raw) {
    if (m_targetSize.isEmpty()) return;
    if (!isFrameUsable(raw)) return;

    const QImage colorized = colorizeGrayFrame(raw);
    if (colorized.isNull()) return;

    QImage finalImage = colorized;
    if (colorized.size() != m_targetSize) {
        finalImage = RgaImage(colorized).scaled(m_targetSize).toQImage();
    }

    VisualFrame visual;
    visual.image = finalImage;
    visual.hot_spot = mapPointToTarget(raw.hot_spot, raw.w, raw.h);
    visual.cold_spot = mapPointToTarget(raw.cold_spot, raw.w, raw.h);
    visual.center_spot = mapPointToTarget(raw.center_spot, raw.w, raw.h);

    emit frameReady(visual);
}

bool ThermalProcessor::isFrameUsable(const RawFrame& raw) const {
    if (raw.pixelFormat != ThermalPixelFormat::Gray8) return false;
    if (raw.w <= 0 || raw.h <= 0) return false;
    if (raw.strideBytes < raw.w) return false;

    const qint64 requiredBytes = static_cast<qint64>(raw.strideBytes) * raw.h;
    if (requiredBytes <= 0) return false;
    return raw.pixelData.size() >= requiredBytes;
}

QImage ThermalProcessor::colorizeGrayFrame(const RawFrame& raw) const {
    QImage out(raw.w, raw.h, QImage::Format_ARGB32);
    if (out.isNull()) return out;

    const auto& table = ThermalPalette::lut(m_palette);
    const uchar* srcBase = reinterpret_cast<const uchar*>(raw.pixelData.constData());

    for (int y = 0; y < raw.h; ++y) {
        const uchar* srcRow = srcBase + (static_cast<qint64>(y) * raw.strideBytes);
        QRgb* dstRow = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < raw.w; ++x) {
            dstRow[x] = static_cast<QRgb>(table[srcRow[x]]);
        }
    }

    return out;
}

TempPt ThermalProcessor::mapPointToTarget(const TempPt& source, int srcW, int srcH) const {
    if (srcW <= 0 || srcH <= 0 || m_targetSize.isEmpty()) return source;

    const float sx = static_cast<float>(m_targetSize.width()) / srcW;
    const float sy = static_cast<float>(m_targetSize.height()) / srcH;

    const int mappedX = qBound(0, qRound(source.x * sx), m_targetSize.width() - 1);
    const int mappedY = qBound(0, qRound(source.y * sy), m_targetSize.height() - 1);
    return TempPt{mappedX, mappedY, source.temperature};
}
