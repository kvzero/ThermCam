#include "thermal_camera.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <cstring>

#include "core/event_bus.h"
#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_frame.h"
#include "seekcamera/seekcamera_manager.h"
#include "seekframe/seekframe.h"

namespace {
constexpr float kMinEmissivity = 0.01f;
constexpr float kMaxEmissivity = 1.0f;

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

    void setPipelineMode(ThermalCamera::PipelineMode mode) {
        QMutexLocker lock(&mutex);
        cachedPipelineMode = mode;
        if (camera) {
            seekcamera_set_pipeline_mode(camera, toSeekPipelineMode(cachedPipelineMode));
        }
    }

    void setEmissivity(float value) {
        value = qBound(kMinEmissivity, value, kMaxEmissivity);

        {
            QMutexLocker lock(&mutex);
            cachedEmissivity = value;
            if (camera) {
                seekcamera_set_scene_emissivity(camera, value);
            }
        }

        emit EventBus::instance().emissivityChanged(value);
    }

    float getEmissivity() const {
        QMutexLocker lock(&mutex);
        return cachedEmissivity;
    }

    void triggerShutter() {
        QMutexLocker lock(&mutex);
        if (camera) {
            seekcamera_shutter_trigger(camera);
        }
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
                seekcamera_set_scene_emissivity(camera, cachedEmissivity);
                seekcamera_set_pipeline_mode(camera, toSeekPipelineMode(cachedPipelineMode));

                seekcamera_register_frame_available_callback(
                    cam, &Impl::onFrameCallback, this);

                const int frameMask = SEEKCAMERA_FRAME_FORMAT_GRAYSCALE |
                                      SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT;
                const seekcamera_error_t startStatus =
                    seekcamera_capture_session_start(cam, frameMask);
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

void ThermalCamera::setPipelineMode(PipelineMode mode) {
    m_impl->setPipelineMode(mode);
}

void ThermalCamera::setEmissivity(float value) {
    m_impl->setEmissivity(value);
}

float ThermalCamera::getEmissivity() const {
    return m_impl->getEmissivity();
}

void ThermalCamera::triggerShutter() {
    m_impl->triggerShutter();
}
