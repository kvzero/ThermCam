#ifndef CAPTURE_WORKER_H
#define CAPTURE_WORKER_H

#include <QObject>
#include <QString>
#include "core/types.h"
#include "ui/widgets/thermal_marker.h"
#include "media/video_encoder.h"

/**
 * @brief Heavy-lifting execution layer for media processing.
 * Runs strictly in a dedicated background QThread to prevent UI blocking.
 */
class CaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit CaptureWorker(QObject* parent = nullptr);
    ~CaptureWorker() = default;

public slots:
    void processPhoto(const VisualFrame& frame, const QString& path);

    void startVideo(const QString& path);
    void stopVideo();
    void processVideoFrame(const VisualFrame& frame);

signals:
    /** @brief Acknowledges completion to decrement the backpressure counter. */
    void frameProcessed();

private:
    void applyOsd(QImage& target, const VisualFrame& frame);

    VideoEncoder m_videoEncoder;
    bool m_isRecording = false;

    /* Isolated instances strictly for off-screen rendering */
    ThermalMarker m_hotMarker{ThermalMarker::Hot};
    ThermalMarker m_coldMarker{ThermalMarker::Cold};
    ThermalMarker m_centerMarker{ThermalMarker::Center};
};

#endif // CAPTURE_WORKER_H
