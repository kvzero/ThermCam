#include "capture_worker.h"
#include "core/global_context.h"
#include "media/image_encoder.h"
#include <QPainter>
#include <QDebug>

CaptureWorker::CaptureWorker(QObject* parent) : QObject(parent) {}

void CaptureWorker::processPhoto(const VisualFrame& frame, const QString& path) {
    /*
     * Qt Copy-on-Write Detachment:
     * 'frame.image' is shared with CameraView. QPainter requires a mutable target.
     * Force a deep copy so we don't draw UI elements on the live screen stream.
     */
    QImage safeImg = frame.image.copy();

    applyOsd(safeImg, frame);
    ImageEncoder::save(safeImg, path, frame);

    emit frameProcessed();
}

void CaptureWorker::startVideo(const QString& path) {
    const QSize s = GlobalContext::instance().screenSize();

    // Safety Net: Hardware video encoders require macroblock alignment (Mod-16)
    // Prevents FFmpeg memory corruption on odd screen resolutions (e.g. 854x480).
    const int alignedW = (s.width() + 15) & (~15);
    const int alignedH = (s.height() + 15) & (~15);

    m_isRecording = m_videoEncoder.open(path, alignedW, alignedH);
    if (!m_isRecording) {
        qCritical() << "[Worker] FATAL: Failed to initialize FFmpeg pipeline.";
    }
}

void CaptureWorker::stopVideo() {
    if (m_isRecording) {
        m_videoEncoder.close();
        m_isRecording = false;
    }
}

void CaptureWorker::processVideoFrame(const VisualFrame& frame) {
    if (!m_isRecording) {
        emit frameProcessed();
        return;
    }

    QImage safeImg = frame.image.copy();

    applyOsd(safeImg, frame);
    m_videoEncoder.writeFrame(safeImg);

    emit frameProcessed();
}

void CaptureWorker::applyOsd(QImage& target, const VisualFrame& frame) {
    QPainter p(&target);

    m_hotMarker.update(frame.hot_spot);
    m_coldMarker.update(frame.cold_spot);
    m_centerMarker.update(frame.center_spot);

    // Reuse the existing robust marker drawing logic off-screen
    m_hotMarker.paint(p, target.size());
    m_coldMarker.paint(p, target.size());
    m_centerMarker.paint(p, target.size());
}
