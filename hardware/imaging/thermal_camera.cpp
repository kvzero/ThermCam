#include "thermal_camera.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>
#include <cstring>
#include <functional>
#include <utility>

#include "core/event_bus.h"
#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_error.h"
#include "seekcamera/seekcamera_frame.h"
#include "seekcamera/seekcamera_manager.h"
#include "seekframe/seekframe.h"

namespace {
constexpr float kMinEmissivity = 0.01f;
constexpr float kMaxEmissivity = 1.0f;
constexpr int kCaptureFrameMask = SEEKCAMERA_FRAME_FORMAT_GRAYSCALE |
                                  SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT;

constexpr seekcamera_histeq_agc_gain_limit_factor_mode_t kHistEqGainFactorModeAuto =
    SEEKCAMERA_HISTEQ_AGC_GAIN_LIMIT_FACTOR_MODE_AUTO;

seekcamera_pipeline_mode_t toSeekPipelineMode(ThermalCamera::PipelineMode mode) {
    switch (mode) {
    case ThermalCamera::PipelineMode::Lite:
        return SEEKCAMERA_IMAGE_LITE;
    case ThermalCamera::PipelineMode::Legacy:
        return SEEKCAMERA_IMAGE_LEGACY;
    case ThermalCamera::PipelineMode::SeekVision:
    default:
        return SEEKCAMERA_IMAGE_SEEKVISION;
    }
}

seekcamera_shutter_mode_t toSeekShutterMode(ThermalCamera::ShutterMode mode) {
    return (mode == ThermalCamera::ShutterMode::Manual)
               ? SEEKCAMERA_SHUTTER_MODE_MANUAL
               : SEEKCAMERA_SHUTTER_MODE_AUTO;
}

seekcamera_agc_mode_t toSeekAgcMode(ThermalCamera::AgcMode mode) {
    return (mode == ThermalCamera::AgcMode::Linear)
               ? SEEKCAMERA_AGC_MODE_LINEAR
               : SEEKCAMERA_AGC_MODE_HISTEQ;
}

seekcamera_filter_state_t toSeekFilterState(bool enabled) {
    return enabled ? SEEKCAMERA_FILTER_STATE_ENABLED : SEEKCAMERA_FILTER_STATE_DISABLED;
}

QString seekErrorString(seekcamera_error_t status) {
    const char* text = seekcamera_error_get_str(status);
    if (!text) return QStringLiteral("Unknown error");
    return QString::fromLatin1(text);
}

QString buildSeekApplyError(const char* action, seekcamera_error_t status) {
    return QStringLiteral("%1 failed: %2 (%3)")
        .arg(QString::fromLatin1(action), seekErrorString(status))
        .arg(static_cast<int>(status));
}
} // namespace

class ThermalCamera::ThermalCameraBackend {
public:
    ThermalCameraBackend() = default;
    ~ThermalCameraBackend() { shutdown(); }

    ThermalCameraBackend(const ThermalCameraBackend&) = delete;
    ThermalCameraBackend& operator=(const ThermalCameraBackend&) = delete;

    void startSeekUsb() {
        seekcamera_error_t status = seekcamera_manager_create(&m_manager, SEEKCAMERA_IO_TYPE_USB);
        if (status != SEEKCAMERA_SUCCESS) {
            qCritical() << "ThermalCamera: failed to create manager:" << status
                        << seekErrorString(status);
            return;
        }

        // The SDK can emit CONNECT synchronously here, so this must not run under m_mutex.
        status = seekcamera_manager_register_event_callback(
            m_manager, &ThermalCameraBackend::onEventCallback, this);
        if (status != SEEKCAMERA_SUCCESS) {
            qCritical() << "ThermalCamera: failed to register manager callback:" << status
                        << seekErrorString(status);
            return;
        }
    }

    void shutdown() {
        seekcamera_manager_t* manager = nullptr;
        {
            QMutexLocker lock(&m_mutex);
            manager = m_manager;
            m_manager = nullptr;
            m_camera = nullptr;
            m_frameCallbackRegistered = false;
            m_cameraObject = nullptr;
        }

        if (manager) {
            seekcamera_manager_destroy(&manager);
        }
    }

    void attach(ThermalCamera* cameraObject) {
        bool notifyConnected = false;
        QString serial;

        {
            QMutexLocker lock(&m_mutex);
            m_cameraObject = cameraObject;
            if (m_camera) {
                notifyConnected = true;
                serial = m_connectedSerial;
            }
        }

        registerFrameCallbackIfNeeded();

        if (notifyConnected) {
            emit cameraObject->cameraConnected(serial);
        }
    }

    void detach() {
        QMutexLocker lock(&m_mutex);
        m_cameraObject = nullptr;
    }

    bool setPipelineMode(ThermalCamera::PipelineMode mode, QString* outError) {
        QMutexLocker lock(&m_mutex);
        const ThermalCamera::PipelineMode previousPipeline = m_cachedPipelineMode;
        m_cachedPipelineMode = mode;

        if (!m_camera) return true;
        if (!prepareSeekVisionEntryLocked(previousPipeline, outError)) return false;
        if (!applyPipelineLocked(outError)) return false;

        if (m_cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) {
            if (!applySharpenFilterLocked(outError)) return false;
            if (!applyAgcModeLocked(outError)) return false;
        }

        return true;
    }

    void setEmissivity(float value) {
        value = qBound(kMinEmissivity, value, kMaxEmissivity);

        {
            QMutexLocker lock(&m_mutex);
            m_cachedEmissivity = value;
            if (m_camera) {
                const seekcamera_error_t status =
                    seekcamera_set_scene_emissivity(m_camera, value);
                if (status != SEEKCAMERA_SUCCESS) {
                    qWarning() << "ThermalCamera: set emissivity failed:" << status
                               << seekErrorString(status);
                }
            }
        }

        emit EventBus::instance().emissivityChanged(value);
    }

    float getEmissivity() const {
        QMutexLocker lock(&m_mutex);
        return m_cachedEmissivity;
    }

    bool setShutterMode(ThermalCamera::ShutterMode mode, QString* outError) {
        QMutexLocker lock(&m_mutex);
        m_cachedShutterMode = mode;
        if (!m_camera) return true;
        return applyShutterModeLocked(outError);
    }

    bool setThermographyOffsetCelsius(float offset, QString* outError) {
        QMutexLocker lock(&m_mutex);
        m_cachedThermographyOffsetCelsius = offset;
        if (!m_camera) return true;
        return applyThermographyOffsetLocked(outError);
    }

    bool setSharpenFilterEnabled(bool enabled, QString* outError) {
        QMutexLocker lock(&m_mutex);
        m_cachedSharpenFilterEnabled = enabled;
        if (!m_camera || m_cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) {
            return true;
        }
        return applySharpenFilterLocked(outError);
    }

    bool setAgcMode(ThermalCamera::AgcMode mode, QString* outError) {
        QMutexLocker lock(&m_mutex);
        m_cachedAgcMode = mode;
        if (!m_camera || m_cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) {
            return true;
        }
        return applyAgcModeLocked(outError);
    }

    bool setLinearAgcManualRangeCelsius(float minCelsius,
                                        float maxCelsius,
                                        QString* outError) {
        QMutexLocker lock(&m_mutex);
        m_cachedLinearAgcMinCelsius = minCelsius;
        m_cachedLinearAgcMaxCelsius = maxCelsius;

        if (!m_camera || m_cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) {
            return true;
        }
        if (m_cachedAgcMode != ThermalCamera::AgcMode::Linear) return true;
        return applyLinearAgcRangeLocked(outError);
    }

    bool triggerShutter(QString* outError) {
        QMutexLocker lock(&m_mutex);
        if (!m_camera) {
            if (outError) *outError = QStringLiteral("Thermal camera is unavailable");
            return false;
        }

        const seekcamera_error_t status = seekcamera_shutter_trigger(m_camera);
        return reportSeekStatus("trigger shutter", status, outError);
    }

    bool triggerFlatSceneCorrection(QString* outError) {
        QMutexLocker lock(&m_mutex);
        if (!m_camera) {
            if (outError) *outError = QStringLiteral("Thermal camera is unavailable");
            return false;
        }

        const bool wasActive = seekcamera_is_active(m_camera);
        if (wasActive) {
            const seekcamera_error_t stopStatus = seekcamera_capture_session_stop(m_camera);
            if (!reportSeekStatus("stop capture session before FSC store",
                                  stopStatus,
                                  outError)) {
                return false;
            }
        }

        const seekcamera_error_t startStatus =
            seekcamera_capture_session_start(m_camera, kCaptureFrameMask);
        if (startStatus != SEEKCAMERA_SUCCESS) {
            reportSeekStatus("start capture session for FSC store", startStatus, outError);
            if (wasActive) {
                const seekcamera_error_t restoreStatus =
                    seekcamera_capture_session_start(m_camera, kCaptureFrameMask);
                if (restoreStatus != SEEKCAMERA_SUCCESS && outError) {
                    const QString restoreError = buildSeekApplyError(
                        "restore capture session after failed FSC start", restoreStatus);
                    if (!outError->isEmpty()) {
                        *outError += QStringLiteral("; ");
                    }
                    *outError += restoreError;
                }
            }
            return false;
        }

        const seekcamera_error_t storeStatus = seekcamera_store_flat_scene_correction(
            m_camera,
            SEEKCAMERA_FLAT_SCENE_CORRECTION_ID_0,
            nullptr,
            nullptr);

        const seekcamera_error_t stopFscStatus = seekcamera_capture_session_stop(m_camera);
        const seekcamera_error_t restartStatus = wasActive
                                                    ? seekcamera_capture_session_start(
                                                          m_camera, kCaptureFrameMask)
                                                    : SEEKCAMERA_SUCCESS;

        if (!reportSeekStatus("store flat-scene correction", storeStatus, outError)) {
            return false;
        }
        if (!reportSeekStatus("stop capture session after FSC store", stopFscStatus, outError)) {
            return false;
        }
        if (wasActive &&
            !reportSeekStatus("restart capture session after FSC store", restartStatus, outError)) {
            return false;
        }

        return true;
    }

private:
    static void onEventCallback(seekcamera_t* camera,
                                seekcamera_manager_event_t event,
                                seekcamera_error_t status,
                                void* userData) {
        auto* self = static_cast<ThermalCameraBackend*>(userData);
        if (self) {
            self->handleEvent(camera, event, status);
        }
    }

    static void onFrameCallback(seekcamera_t* camera,
                                seekcamera_frame_t* frame,
                                void* userData) {
        Q_UNUSED(camera);
        auto* self = static_cast<ThermalCameraBackend*>(userData);
        if (self) {
            self->handleFrame(frame);
        }
    }

    void handleEvent(seekcamera_t* camera,
                     seekcamera_manager_event_t event,
                     seekcamera_error_t status) {
        ThermalCamera* notifyConnected = nullptr;
        ThermalCamera* notifyDisconnected = nullptr;
        QString connectedSerial;
        QString disconnectedReason;

        switch (event) {
        case SEEKCAMERA_MANAGER_EVENT_CONNECT: {
            seekcamera_chipid_t chipStatusBuffer;
            const seekcamera_error_t chipStatus =
                seekcamera_get_chipid(camera, &chipStatusBuffer);
            connectedSerial = (chipStatus == SEEKCAMERA_SUCCESS)
                                  ? QString::fromLatin1(chipStatusBuffer)
                                  : QStringLiteral("Unknown");

            {
                QMutexLocker lock(&m_mutex);
                m_camera = camera;
                m_connectedSerial = connectedSerial;
                m_frameCallbackRegistered = false;
            }

            {
                QMutexLocker lock(&m_mutex);
                applyCachedCameraStateOnConnectLocked();
            }

            registerFrameCallbackIfNeeded();

            const seekcamera_error_t startStatus =
                seekcamera_capture_session_start(camera, kCaptureFrameMask);
            if (startStatus != SEEKCAMERA_SUCCESS) {
                qWarning() << "ThermalCamera: capture session start failed:" << startStatus
                           << seekErrorString(startStatus);
            }

            {
                QMutexLocker lock(&m_mutex);
                notifyConnected = m_cameraObject;
            }
            break;
        }

        case SEEKCAMERA_MANAGER_EVENT_DISCONNECT:
            disconnectedReason = QStringLiteral("Device removed");
            {
                QMutexLocker lock(&m_mutex);
                m_camera = nullptr;
                m_frameCallbackRegistered = false;
                notifyDisconnected = m_cameraObject;
            }
            break;

        case SEEKCAMERA_MANAGER_EVENT_ERROR:
            qWarning() << "ThermalCamera: manager error:" << status
                       << seekErrorString(status);
            disconnectedReason = QStringLiteral("Internal error");
            {
                QMutexLocker lock(&m_mutex);
                notifyDisconnected = m_cameraObject;
            }
            break;

        default:
            break;
        }

        if (notifyConnected) {
            emit notifyConnected->cameraConnected(connectedSerial);
        }
        if (notifyDisconnected) {
            emit notifyDisconnected->cameraDisconnected(disconnectedReason);
        }
    }

    void handleFrame(seekcamera_frame_t* frame) {
        ThermalCamera* cameraObject = nullptr;

        {
            QMutexLocker lock(&m_mutex);
            cameraObject = m_cameraObject;
            if (!cameraObject) return;
        }

        if (seekcamera_frame_lock(frame) != SEEKCAMERA_SUCCESS) {
            return;
        }

        RawFrame output;
        extractGrayFrame(frame, &output);
        extractThermographyPoints(frame, &output);

        seekcamera_frame_unlock(frame);

        if (!output.pixelData.isEmpty()) {
            emit cameraObject->rawFrameReady(output);
        }
    }

    bool registerFrameCallbackIfNeeded() {
        seekcamera_t* camera = nullptr;
        bool shouldRegister = false;

        {
            QMutexLocker lock(&m_mutex);
            camera = m_camera;
            shouldRegister = camera && m_cameraObject && !m_frameCallbackRegistered;
        }

        if (!shouldRegister) return true;

        const seekcamera_error_t status =
            seekcamera_register_frame_available_callback(
                camera, &ThermalCameraBackend::onFrameCallback, this);
        if (status != SEEKCAMERA_SUCCESS) {
            qWarning() << "ThermalCamera: failed to register frame callback:" << status
                       << seekErrorString(status);
            return false;
        }

        {
            QMutexLocker lock(&m_mutex);
            if (m_camera == camera) {
                m_frameCallbackRegistered = true;
            }
        }

        return true;
    }

    bool reportSeekStatus(const char* action,
                          seekcamera_error_t status,
                          QString* outError) const {
        if (status == SEEKCAMERA_SUCCESS) return true;
        if (outError) *outError = buildSeekApplyError(action, status);
        return false;
    }

    bool applyPipelineLocked(QString* outError) const {
        return reportSeekStatus("set pipeline mode",
                                seekcamera_set_pipeline_mode(
                                    m_camera, toSeekPipelineMode(m_cachedPipelineMode)),
                                outError);
    }

    bool prepareSeekVisionEntryLocked(ThermalCamera::PipelineMode previousPipeline,
                                      QString* outError) const {
        if (previousPipeline == ThermalCamera::PipelineMode::SeekVision) return true;
        if (m_cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) return true;
        if (m_cachedAgcMode != ThermalCamera::AgcMode::Linear) return true;

        const seekcamera_error_t status =
            seekcamera_set_agc_mode(m_camera, SEEKCAMERA_AGC_MODE_HISTEQ);
        if (status == SEEKCAMERA_SUCCESS || status == SEEKCAMERA_ERROR_NOT_SUPPORTED) {
            return true;
        }

        return reportSeekStatus("prepare AGC for SeekVision transition", status, outError);
    }

    bool applyShutterModeLocked(QString* outError) const {
        return reportSeekStatus("set shutter mode",
                                seekcamera_set_shutter_mode(
                                    m_camera, toSeekShutterMode(m_cachedShutterMode)),
                                outError);
    }

    bool applyThermographyOffsetLocked(QString* outError) const {
        return reportSeekStatus("set thermography offset",
                                seekcamera_set_thermography_offset(
                                    m_camera, m_cachedThermographyOffsetCelsius),
                                outError);
    }

    bool applySharpenFilterLocked(QString* outError) const {
        return reportSeekStatus("set sharpen filter",
                                seekcamera_set_filter_state(
                                    m_camera,
                                    SEEKCAMERA_FILTER_SHARPEN_CORRECTION,
                                    toSeekFilterState(m_cachedSharpenFilterEnabled)),
                                outError);
    }

    bool applyHistEqAutoDefaultsLocked(QString* outError) const {
        return reportSeekStatus("set HistEQ gain factor mode",
                                seekcamera_set_histeq_agc_gain_limit_factor_mode(
                                    m_camera, kHistEqGainFactorModeAuto),
                                outError);
    }

    bool applyLinearAgcRangeLocked(QString* outError) const {
        if (!reportSeekStatus("set Linear AGC lock mode",
                              seekcamera_set_linear_agc_lock_mode(
                                  m_camera, SEEKCAMERA_LINEAR_AGC_LOCK_MODE_MANUAL),
                              outError)) {
            return false;
        }

        if (!reportSeekStatus("set Linear AGC min",
                              seekcamera_set_linear_agc_lock_min(
                                  m_camera, m_cachedLinearAgcMinCelsius),
                              outError)) {
            return false;
        }

        if (!reportSeekStatus("set Linear AGC max",
                              seekcamera_set_linear_agc_lock_max(
                                  m_camera, m_cachedLinearAgcMaxCelsius),
                              outError)) {
            return false;
        }

        return true;
    }

    bool applyAgcModeLocked(QString* outError) const {
        if (!reportSeekStatus("set AGC mode",
                              seekcamera_set_agc_mode(
                                  m_camera, toSeekAgcMode(m_cachedAgcMode)),
                              outError)) {
            return false;
        }

        if (m_cachedAgcMode == ThermalCamera::AgcMode::Linear) {
            return applyLinearAgcRangeLocked(outError);
        }

        return applyHistEqAutoDefaultsLocked(outError);
    }

    void applyCachedCameraStateOnConnectLocked() {
        auto applyOrLog = [this](const char* title, const std::function<bool(QString*)>& fn) {
            QString error;
            if (!fn(&error)) {
                qWarning() << "ThermalCamera:" << title << "failed:" << error;
            }
        };

        applyOrLog("emissivity", [this](QString* outError) {
            return reportSeekStatus("set emissivity",
                                    seekcamera_set_scene_emissivity(
                                        m_camera, m_cachedEmissivity),
                                    outError);
        });

        applyOrLog("pipeline", [this](QString* outError) {
            return applyPipelineLocked(outError);
        });

        applyOrLog("shutter mode", [this](QString* outError) {
            return applyShutterModeLocked(outError);
        });

        applyOrLog("thermography offset", [this](QString* outError) {
            return applyThermographyOffsetLocked(outError);
        });

        if (m_cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) {
            applyOrLog("sharpen filter", [this](QString* outError) {
                return applySharpenFilterLocked(outError);
            });

            applyOrLog("agc mode", [this](QString* outError) {
                return applyAgcModeLocked(outError);
            });
        }
    }

    void extractGrayFrame(seekcamera_frame_t* cameraFrame, RawFrame* out) const {
        if (!out) return;

        seekframe_t* grayFrame = nullptr;
        const seekcamera_error_t status = seekcamera_frame_get_frame_by_format(
            cameraFrame, SEEKCAMERA_FRAME_FORMAT_GRAYSCALE, &grayFrame);
        if (status != SEEKCAMERA_SUCCESS || !grayFrame) {
            return;
        }

        const size_t width = seekframe_get_width(grayFrame);
        const size_t height = seekframe_get_height(grayFrame);
        const size_t sourceStride = seekframe_get_line_stride(grayFrame);
        if (width == 0 || height == 0 || sourceStride < width) {
            return;
        }

        const int w = static_cast<int>(width);
        const int h = static_cast<int>(height);
        out->w = w;
        out->h = h;
        out->strideBytes = w;
        out->pixelFormat = ThermalPixelFormat::Gray8;
        out->pixelData.resize(w * h);

        uchar* dstBase = reinterpret_cast<uchar*>(out->pixelData.data());
        for (size_t y = 0; y < height; ++y) {
            const uchar* srcRow =
                reinterpret_cast<const uchar*>(seekframe_get_row(grayFrame, y));
            if (!srcRow) {
                out->pixelData.clear();
                return;
            }

            uchar* dstRow = dstBase + (y * width);
            memcpy(dstRow, srcRow, width);
        }
    }

    void extractThermographyPoints(seekcamera_frame_t* cameraFrame, RawFrame* out) const {
        if (!out) return;

        seekframe_t* thermoFrame = nullptr;
        const seekcamera_error_t status = seekcamera_frame_get_frame_by_format(
            cameraFrame, SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT, &thermoFrame);
        if (status != SEEKCAMERA_SUCCESS || !thermoFrame) {
            return;
        }

        auto* header = static_cast<seekcamera_frame_header_t*>(seekframe_get_header(thermoFrame));
        if (!header) {
            return;
        }

        out->hot_spot.x = header->thermography_max_x;
        out->hot_spot.y = header->thermography_max_y;
        out->hot_spot.temperature = header->thermography_max_value;

        out->cold_spot.x = header->thermography_min_x;
        out->cold_spot.y = header->thermography_min_y;
        out->cold_spot.temperature = header->thermography_min_value;

        out->center_spot.x = header->thermography_spot_x;
        out->center_spot.y = header->thermography_spot_y;
        out->center_spot.temperature = header->thermography_spot_value;
    }

private:
    seekcamera_manager_t* m_manager = nullptr;
    seekcamera_t* m_camera = nullptr;

    mutable QMutex m_mutex;
    ThermalCamera* m_cameraObject = nullptr;
    QString m_connectedSerial;

    float m_cachedEmissivity = 0.95f;
    ThermalCamera::PipelineMode m_cachedPipelineMode = ThermalCamera::PipelineMode::SeekVision;
    ThermalCamera::ShutterMode m_cachedShutterMode = ThermalCamera::ShutterMode::Auto;
    float m_cachedThermographyOffsetCelsius = 0.0f;
    bool m_cachedSharpenFilterEnabled = false;
    ThermalCamera::AgcMode m_cachedAgcMode = ThermalCamera::AgcMode::HistEq;
    float m_cachedLinearAgcMinCelsius = 20.0f;
    float m_cachedLinearAgcMaxCelsius = 80.0f;

    bool m_frameCallbackRegistered = false;
};

ThermalCamera::StartupHandle::StartupHandle(
    std::unique_ptr<ThermalCamera::ThermalCameraBackend> backend)
    : m_backend(std::move(backend)) {}

ThermalCamera::StartupHandle::~StartupHandle() = default;

ThermalCamera::StartupHandle::StartupHandle(StartupHandle&& other) noexcept = default;

ThermalCamera::StartupHandle&
ThermalCamera::StartupHandle::operator=(StartupHandle&& other) noexcept = default;

ThermalCamera::StartupHandle ThermalCamera::startSeekUsbEarly() {
    auto backend = std::make_unique<ThermalCameraBackend>();
    backend->startSeekUsb();
    return StartupHandle(std::move(backend));
}

ThermalCamera::ThermalCamera(QObject* parent)
    : QObject(parent),
      m_backend(std::make_unique<ThermalCameraBackend>()) {
    m_backend->startSeekUsb();
    m_backend->attach(this);
}

ThermalCamera::ThermalCamera(StartupHandle startup, QObject* parent)
    : QObject(parent),
      m_backend(std::move(startup.m_backend)) {
    m_backend->attach(this);
}

ThermalCamera::~ThermalCamera() {
    m_backend->detach();
}

bool ThermalCamera::setPipelineMode(PipelineMode mode, QString* outError) {
    QString localError;
    QString* errSink = outError ? outError : &localError;
    const bool ok = m_backend->setPipelineMode(mode, errSink);
    if (!ok && !outError) {
        qWarning() << "ThermalCamera: set pipeline mode failed:" << localError;
    }
    return ok;
}

void ThermalCamera::setEmissivity(float value) {
    m_backend->setEmissivity(value);
}

float ThermalCamera::getEmissivity() const {
    return m_backend->getEmissivity();
}

bool ThermalCamera::setShutterMode(ShutterMode mode, QString* outError) {
    return m_backend->setShutterMode(mode, outError);
}

bool ThermalCamera::setThermographyOffsetCelsius(float offset, QString* outError) {
    return m_backend->setThermographyOffsetCelsius(offset, outError);
}

bool ThermalCamera::setSharpenFilterEnabled(bool enabled, QString* outError) {
    return m_backend->setSharpenFilterEnabled(enabled, outError);
}

bool ThermalCamera::setAgcMode(AgcMode mode, QString* outError) {
    return m_backend->setAgcMode(mode, outError);
}

bool ThermalCamera::setLinearAgcManualRangeCelsius(float minCelsius,
                                                   float maxCelsius,
                                                   QString* outError) {
    return m_backend->setLinearAgcManualRangeCelsius(minCelsius, maxCelsius, outError);
}

void ThermalCamera::triggerShutter() {
    QString error;
    if (!m_backend->triggerShutter(&error)) {
        qWarning() << "ThermalCamera: trigger shutter failed:" << error;
    }
}

bool ThermalCamera::triggerFlatSceneCorrection(QString* outError) {
    return m_backend->triggerFlatSceneCorrection(outError);
}
