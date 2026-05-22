#include "thermal_processor.h"

#include "hardware/rga/rga_image.h"

#include <QtGlobal>
#include <QReadLocker>
#include <QWriteLocker>
#include <cstring>

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

    cacheLatestGrayFrame(raw);

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

QImage ThermalProcessor::renderPreview(ThermalPalette::Id id, const QSize& size) const {
    if (id == ThermalPalette::Id::Count || size.isEmpty()) return QImage();

    LatestGrayFrame snapshot;
    {
        QReadLocker lock(&m_grayLock);
        snapshot = m_latestGray;
    }

    if (snapshot.pixelData.isEmpty() || snapshot.w <= 0 || snapshot.h <= 0 ||
        snapshot.strideBytes < snapshot.w) {
        return QImage();
    }

    QImage grayView(reinterpret_cast<const uchar*>(snapshot.pixelData.constData()),
                    snapshot.w,
                    snapshot.h,
                    snapshot.strideBytes,
                    QImage::Format_Grayscale8);
    if (grayView.isNull()) return QImage();

    QImage scaledGray = grayView;
    if (grayView.size() != size) {
        scaledGray = grayView.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    } else {
        scaledGray = grayView.copy();
    }
    if (scaledGray.isNull()) return QImage();

    if (scaledGray.format() != QImage::Format_Grayscale8) {
        scaledGray = scaledGray.convertToFormat(QImage::Format_Grayscale8);
    }

    return colorizeGrayImage(scaledGray, id);
}

bool ThermalProcessor::isFrameUsable(const RawFrame& raw) const {
    if (raw.pixelFormat != ThermalPixelFormat::Gray8) return false;
    if (raw.w <= 0 || raw.h <= 0) return false;
    if (raw.strideBytes < raw.w) return false;

    const qint64 requiredBytes = static_cast<qint64>(raw.strideBytes) * raw.h;
    if (requiredBytes <= 0) return false;
    return raw.pixelData.size() >= requiredBytes;
}

void ThermalProcessor::cacheLatestGrayFrame(const RawFrame& raw) {
    LatestGrayFrame latest;
    latest.w = raw.w;
    latest.h = raw.h;
    latest.strideBytes = raw.strideBytes;
    latest.pixelData.resize(raw.strideBytes * raw.h);
    memcpy(latest.pixelData.data(), raw.pixelData.constData(), latest.pixelData.size());

    QWriteLocker lock(&m_grayLock);
    m_latestGray = latest;
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

QImage ThermalProcessor::colorizeGrayImage(const QImage& grayImage, ThermalPalette::Id id) const {
    if (grayImage.isNull()) return QImage();
    if (id == ThermalPalette::Id::Count) return QImage();

    QImage gray = grayImage;
    if (gray.format() != QImage::Format_Grayscale8) {
        gray = gray.convertToFormat(QImage::Format_Grayscale8);
    }
    if (gray.isNull()) return QImage();

    QImage out(gray.width(), gray.height(), QImage::Format_ARGB32);
    if (out.isNull()) return out;

    const auto& table = ThermalPalette::lut(id);

    for (int y = 0; y < gray.height(); ++y) {
        const uchar* srcRow = gray.constScanLine(y);
        QRgb* dstRow = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < gray.width(); ++x) {
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
