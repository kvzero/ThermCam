#include "thermal_camera.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <cstring>
#include <functional>

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

class ThermalCamera::Impl {
public:
    explicit Impl(ThermalCamera* cameraObject) : q(cameraObject) {}
    ~Impl() { shutdown(); }

    void init() {
        seekcamera_error_t status = seekcamera_manager_create(&manager, SEEKCAMERA_IO_TYPE_USB);
        if (status != SEEKCAMERA_SUCCESS) {
            qCritical() << "ThermalCamera: failed to create manager:" << status;
            return;
        }

        status = seekcamera_manager_register_event_callback(
            manager, &Impl::onEventCallback, this);
        if (status != SEEKCAMERA_SUCCESS) {
            qCritical() << "ThermalCamera: failed to register manager callback:" << status;
        }
    }

    void shutdown() {
        QMutexLocker lock(&mutex);
        if (manager) {
            seekcamera_manager_destroy(&manager);
            manager = nullptr;
            camera = nullptr;
        }
    }

    bool setPipelineMode(ThermalCamera::PipelineMode mode, QString* outError) {
        QMutexLocker lock(&mutex);
        const ThermalCamera::PipelineMode previousPipeline = cachedPipelineMode;
        cachedPipelineMode = mode;

        if (!camera) return true;
        if (!prepareSeekVisionEntryLocked(previousPipeline, outError)) return false;
        if (!applyPipelineLocked(outError)) return false;

        if (cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) {
            if (!applySharpenFilterLocked(outError)) return false;
            if (!applyAgcModeLocked(outError)) return false;
        }

        return true;
    }

    void setEmissivity(float value) {
        value = qBound(kMinEmissivity, value, kMaxEmissivity);

        {
            QMutexLocker lock(&mutex);
            cachedEmissivity = value;
            if (camera) {
                const seekcamera_error_t status = seekcamera_set_scene_emissivity(camera, value);
                if (status != SEEKCAMERA_SUCCESS) {
                    qWarning() << "ThermalCamera: set emissivity failed:" << status
                               << seekErrorString(status);
                }
            }
        }

        emit EventBus::instance().emissivityChanged(value);
    }

    float getEmissivity() const {
        QMutexLocker lock(&mutex);
        return cachedEmissivity;
    }

    bool setShutterMode(ThermalCamera::ShutterMode mode, QString* outError) {
        QMutexLocker lock(&mutex);
        cachedShutterMode = mode;
        if (!camera) return true;
        return applyShutterModeLocked(outError);
    }

    bool setThermographyOffsetCelsius(float offset, QString* outError) {
        QMutexLocker lock(&mutex);
        cachedThermographyOffsetCelsius = offset;
        if (!camera) return true;
        return applyThermographyOffsetLocked(outError);
    }

    bool setSharpenFilterEnabled(bool enabled, QString* outError) {
        QMutexLocker lock(&mutex);
        cachedSharpenFilterEnabled = enabled;
        if (!camera || cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) return true;
        return applySharpenFilterLocked(outError);
    }

    bool setAgcMode(ThermalCamera::AgcMode mode, QString* outError) {
        QMutexLocker lock(&mutex);
        cachedAgcMode = mode;
        if (!camera || cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) return true;
        return applyAgcModeLocked(outError);
    }

    bool setLinearAgcManualRangeCelsius(float minCelsius,
                                         float maxCelsius,
                                         QString* outError) {
        QMutexLocker lock(&mutex);
        cachedLinearAgcMinCelsius = minCelsius;
        cachedLinearAgcMaxCelsius = maxCelsius;

        if (!camera || cachedPipelineMode == ThermalCamera::PipelineMode::SeekVision) return true;
        if (cachedAgcMode != ThermalCamera::AgcMode::Linear) return true;
        return applyLinearAgcRangeLocked(outError);
    }

    bool triggerShutter(QString* outError) {
        QMutexLocker lock(&mutex);
        if (!camera) {
            if (outError) *outError = QStringLiteral("Thermal camera is unavailable");
            return false;
        }

        const seekcamera_error_t status = seekcamera_shutter_trigger(camera);
        return reportSeekStatus("trigger shutter", status, outError);
    }

    bool triggerFlatSceneCorrection(QString* outError) {
        QMutexLocker lock(&mutex);
        if (!camera) {
            if (outError) *outError = QStringLiteral("Thermal camera is unavailable");
            return false;
        }

        const bool wasActive = seekcamera_is_active(camera);
        if (wasActive) {
            const seekcamera_error_t stopStatus = seekcamera_capture_session_stop(camera);
            if (!reportSeekStatus("stop capture session before FSC store",
                                  stopStatus,
                                  outError)) {
                return false;
            }
        }

        const seekcamera_error_t startStatus =
            seekcamera_capture_session_start(camera, kCaptureFrameMask);
        if (startStatus != SEEKCAMERA_SUCCESS) {
            reportSeekStatus("start capture session for FSC store", startStatus, outError);
            if (wasActive) {
                const seekcamera_error_t restoreStatus =
                    seekcamera_capture_session_start(camera, kCaptureFrameMask);
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
            camera,
            SEEKCAMERA_FLAT_SCENE_CORRECTION_ID_0,
            nullptr,
            nullptr);

        const seekcamera_error_t stopFscStatus = seekcamera_capture_session_stop(camera);
        const seekcamera_error_t restartStatus = wasActive
                                                    ? seekcamera_capture_session_start(camera,
                                                                                       kCaptureFrameMask)
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
    static void onEventCallback(seekcamera_t* cam,
                                seekcamera_manager_event_t event,
                                seekcamera_error_t status,
                                void* userData) {
        auto* self = static_cast<Impl*>(userData);
        if (self) {
            self->handleEvent(cam, event, status);
        }
    }

    static void onFrameCallback(seekcamera_t* cam, seekcamera_frame_t* frame, void* userData) {
        Q_UNUSED(cam);
        auto* self = static_cast<Impl*>(userData);
        if (self) {
            self->handleFrame(frame);
        }
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
                                seekcamera_set_pipeline_mode(camera,
                                                             toSeekPipelineMode(cachedPipelineMode)),
                                outError);
    }

    bool prepareSeekVisionEntryLocked(ThermalCamera::PipelineMode previousPipeline,
                                      QString* outError) const {
        if (previousPipeline == ThermalCamera::PipelineMode::SeekVision) return true;
        if (cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) return true;
        if (cachedAgcMode != ThermalCamera::AgcMode::Linear) return true;

        // SDK 4.4.x still has Linear AGC known issues on some cores (e.g. stale 320x240 header);
        // pre-switching to HistEQ avoids carrying linear artifacts into SeekVision transitions.
        const seekcamera_error_t status =
            seekcamera_set_agc_mode(camera, SEEKCAMERA_AGC_MODE_HISTEQ);
        if (status == SEEKCAMERA_SUCCESS || status == SEEKCAMERA_ERROR_NOT_SUPPORTED) {
            return true;
        }

        return reportSeekStatus("prepare AGC for SeekVision transition", status, outError);
    }

    bool applyShutterModeLocked(QString* outError) const {
        return reportSeekStatus("set shutter mode",
                                seekcamera_set_shutter_mode(camera,
                                                            toSeekShutterMode(cachedShutterMode)),
                                outError);
    }

    bool applyThermographyOffsetLocked(QString* outError) const {
        return reportSeekStatus("set thermography offset",
                                seekcamera_set_thermography_offset(camera,
                                                                   cachedThermographyOffsetCelsius),
                                outError);
    }

    bool applySharpenFilterLocked(QString* outError) const {
        return reportSeekStatus("set sharpen filter",
                                seekcamera_set_filter_state(camera,
                                                            SEEKCAMERA_FILTER_SHARPEN_CORRECTION,
                                                            toSeekFilterState(cachedSharpenFilterEnabled)),
                                outError);
    }

    bool applyHistEqAutoDefaultsLocked(QString* outError) const {
        return reportSeekStatus("set HistEQ gain factor mode",
                                seekcamera_set_histeq_agc_gain_limit_factor_mode(
                                    camera, kHistEqGainFactorModeAuto),
                                outError);
    }

    bool applyLinearAgcRangeLocked(QString* outError) const {
        if (!reportSeekStatus("set Linear AGC lock mode",
                              seekcamera_set_linear_agc_lock_mode(
                                  camera, SEEKCAMERA_LINEAR_AGC_LOCK_MODE_MANUAL),
                              outError)) {
            return false;
        }

        if (!reportSeekStatus("set Linear AGC min",
                              seekcamera_set_linear_agc_lock_min(camera,
                                                                 cachedLinearAgcMinCelsius),
                              outError)) {
            return false;
        }

        if (!reportSeekStatus("set Linear AGC max",
                              seekcamera_set_linear_agc_lock_max(camera,
                                                                 cachedLinearAgcMaxCelsius),
                              outError)) {
            return false;
        }

        return true;
    }

    bool applyAgcModeLocked(QString* outError) const {
        if (!reportSeekStatus("set AGC mode",
                              seekcamera_set_agc_mode(camera, toSeekAgcMode(cachedAgcMode)),
                              outError)) {
            return false;
        }

        if (cachedAgcMode == ThermalCamera::AgcMode::Linear) {
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
                                    seekcamera_set_scene_emissivity(camera, cachedEmissivity),
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

        if (cachedPipelineMode != ThermalCamera::PipelineMode::SeekVision) {
            applyOrLog("sharpen filter", [this](QString* outError) {
                return applySharpenFilterLocked(outError);
            });

            applyOrLog("agc mode", [this](QString* outError) {
                return applyAgcModeLocked(outError);
            });
        }
    }

    void handleEvent(seekcamera_t* cam,
                     seekcamera_manager_event_t event,
                     seekcamera_error_t status) {
        QString connectedSerial;
        QString disconnectedReason;
        bool shouldEmitConnected = false;
        bool shouldEmitDisconnected = false;

        {
            QMutexLocker lock(&mutex);

            switch (event) {
            case SEEKCAMERA_MANAGER_EVENT_CONNECT: {
                camera = cam;
                applyCachedCameraStateOnConnectLocked();

                seekcamera_register_frame_available_callback(
                    cam, &Impl::onFrameCallback, this);

                const seekcamera_error_t startStatus =
                    seekcamera_capture_session_start(cam, kCaptureFrameMask);
                if (startStatus != SEEKCAMERA_SUCCESS) {
                    qWarning() << "ThermalCamera: capture session start failed:" << startStatus;
                }

                seekcamera_chipid_t chipid;
                const seekcamera_error_t chipStatus = seekcamera_get_chipid(cam, &chipid);
                connectedSerial = (chipStatus == SEEKCAMERA_SUCCESS)
                                      ? QString::fromLatin1(chipid)
                                      : QStringLiteral("Unknown");
                shouldEmitConnected = true;
                break;
            }

            case SEEKCAMERA_MANAGER_EVENT_DISCONNECT:
                camera = nullptr;
                disconnectedReason = QStringLiteral("Device removed");
                shouldEmitDisconnected = true;
                break;

            case SEEKCAMERA_MANAGER_EVENT_ERROR:
                qWarning() << "ThermalCamera: manager error:" << status;
                disconnectedReason = QStringLiteral("Internal error");
                shouldEmitDisconnected = true;
                break;

            default:
                break;
            }
        }

        if (shouldEmitConnected) {
            emit q->cameraConnected(connectedSerial);
        }
        if (shouldEmitDisconnected) {
            emit q->cameraDisconnected(disconnectedReason);
        }
    }

    void handleFrame(seekcamera_frame_t* frame) {
        if (seekcamera_frame_lock(frame) != SEEKCAMERA_SUCCESS) {
            return;
        }

        RawFrame output;
        extractGrayFrame(frame, &output);
        extractThermographyPoints(frame, &output);

        seekcamera_frame_unlock(frame);

        if (!output.pixelData.isEmpty()) {
            emit q->rawFrameReady(output);
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
            const uchar* srcRow = reinterpret_cast<const uchar*>(seekframe_get_row(grayFrame, y));
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
    ThermalCamera* q = nullptr;

    seekcamera_manager_t* manager = nullptr;
    seekcamera_t* camera = nullptr;

    mutable QMutex mutex;
    float cachedEmissivity = 0.95f;
    ThermalCamera::PipelineMode cachedPipelineMode = ThermalCamera::PipelineMode::SeekVision;
    ThermalCamera::ShutterMode cachedShutterMode = ThermalCamera::ShutterMode::Auto;
    float cachedThermographyOffsetCelsius = 0.0f;
    bool cachedSharpenFilterEnabled = false;
    ThermalCamera::AgcMode cachedAgcMode = ThermalCamera::AgcMode::HistEq;
    float cachedLinearAgcMinCelsius = 20.0f;
    float cachedLinearAgcMaxCelsius = 80.0f;
};

ThermalCamera::ThermalCamera(QObject *parent)
    : QObject(parent),
      m_impl(new Impl(this)) {
    m_impl->init();
}

ThermalCamera::~ThermalCamera() {
    delete m_impl;
    m_impl = nullptr;
}

bool ThermalCamera::setPipelineMode(PipelineMode mode, QString* outError) {
    QString localError;
    QString* errSink = outError ? outError : &localError;
    const bool ok = m_impl->setPipelineMode(mode, errSink);
    if (!ok && !outError) {
        qWarning() << "ThermalCamera: set pipeline mode failed:" << localError;
    }
    return ok;
}

void ThermalCamera::setEmissivity(float value) {
    m_impl->setEmissivity(value);
}

float ThermalCamera::getEmissivity() const {
    return m_impl->getEmissivity();
}

bool ThermalCamera::setShutterMode(ShutterMode mode, QString* outError) {
    return m_impl->setShutterMode(mode, outError);
}

bool ThermalCamera::setThermographyOffsetCelsius(float offset, QString* outError) {
    return m_impl->setThermographyOffsetCelsius(offset, outError);
}

bool ThermalCamera::setSharpenFilterEnabled(bool enabled, QString* outError) {
    return m_impl->setSharpenFilterEnabled(enabled, outError);
}

bool ThermalCamera::setAgcMode(AgcMode mode, QString* outError) {
    return m_impl->setAgcMode(mode, outError);
}

bool ThermalCamera::setLinearAgcManualRangeCelsius(float minCelsius,
                                                    float maxCelsius,
                                                    QString* outError) {
    return m_impl->setLinearAgcManualRangeCelsius(minCelsius, maxCelsius, outError);
}

void ThermalCamera::triggerShutter() {
    QString error;
    if (!m_impl->triggerShutter(&error)) {
        qWarning() << "ThermalCamera: trigger shutter failed:" << error;
    }
}

bool ThermalCamera::triggerFlatSceneCorrection(QString* outError) {
    return m_impl->triggerFlatSceneCorrection(outError);
}
