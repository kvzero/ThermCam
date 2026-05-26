#ifndef THERMAL_CAMERA_H
#define THERMAL_CAMERA_H

#include <QObject>
#include <QtGlobal>
#include <QString>

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

    enum class ShutterMode : quint8 {
        Auto = 0,
        Manual = 1
    };

    enum class AgcMode : quint8 {
        HistEq = 0,
        Linear = 1
    };

    explicit ThermalCamera(QObject *parent = nullptr);
    ~ThermalCamera() override;

    bool setPipelineMode(PipelineMode mode, QString* outError = nullptr);
    void setEmissivity(float value);
    float getEmissivity() const;

    bool setShutterMode(ShutterMode mode, QString* outError = nullptr);
    bool setThermographyOffsetCelsius(float offset, QString* outError = nullptr);
    bool setSharpenFilterEnabled(bool enabled, QString* outError = nullptr);
    bool setAgcMode(AgcMode mode, QString* outError = nullptr);
    bool setLinearAgcManualRangeCelsius(float minCelsius,
                                        float maxCelsius,
                                        QString* outError = nullptr);

    void triggerShutter();
    bool triggerFlatSceneCorrection(QString* outError = nullptr);

signals:
    void cameraConnected(const QString& serial);
    void cameraDisconnected(const QString& reason);
    void rawFrameReady(const RawFrame& frame);

private:
    class Impl;
    Impl* m_impl = nullptr;
};

#endif // THERMAL_CAMERA_H
