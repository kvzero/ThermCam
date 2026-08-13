#ifndef THERMAL_CAMERA_H
#define THERMAL_CAMERA_H

#include <QObject>
#include <QtGlobal>
#include <QString>
#include <memory>

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
private:
    class ThermalCameraBackend;

public:
    /**
     * @brief Move-only owner for a Seek USB backend started before QApplication.
     *
     * ThermalCamera adopts this handle after QApplication exists; the handle is
     * not a second public camera API.
     */
    class StartupHandle {
    public:
        ~StartupHandle();

        StartupHandle(StartupHandle&& other) noexcept;
        StartupHandle& operator=(StartupHandle&& other) noexcept;

        StartupHandle(const StartupHandle&) = delete;
        StartupHandle& operator=(const StartupHandle&) = delete;

    private:
        explicit StartupHandle(std::unique_ptr<ThermalCameraBackend> backend);

        std::unique_ptr<ThermalCameraBackend> m_backend;

        friend class ThermalCamera;
    };

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
    explicit ThermalCamera(StartupHandle startup, QObject *parent = nullptr);
    ~ThermalCamera() override;

    static StartupHandle startSeekUsbEarly();

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
    std::unique_ptr<ThermalCameraBackend> m_backend;
};

#endif // THERMAL_CAMERA_H
