#ifndef THERMAL_CAMERA_H
#define THERMAL_CAMERA_H

#include <QObject>
#include <QtGlobal>

#include "core/types.h"

/**
 * @brief Thermal camera hardware abstraction.
 *
 * This class wraps the vendor SDK and exposes a stable project-facing API:
 * - Manages camera connection lifecycle.
 * - Publishes grayscale raw frames for rendering.
 * - Provides basic camera controls.
 */
class ThermalCamera : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Camera-side image processing pipeline modes.
     */
    enum class PipelineMode : quint8 {
        Lite = 0,
        Legacy = 1,
        SeekVision = 2
    };

    explicit ThermalCamera(QObject *parent = nullptr);
    ~ThermalCamera() override;

    void setPipelineMode(PipelineMode mode);
    void setEmissivity(float value);
    float getEmissivity() const;
    void triggerShutter();

signals:
    void cameraConnected(const QString& serial);
    void cameraDisconnected(const QString& reason);
    void rawFrameReady(const RawFrame& frame);

private:
    class Impl;
    Impl* m_impl = nullptr;
};

#endif // THERMAL_CAMERA_H
