#include "seekcam.h"
#include "core/event_bus.h"
#include <QDebug>

ThermalCamera::ThermalCamera(QObject *parent) : QObject(parent) {
    seekcamera_error_t status = seekcamera_manager_create(&m_manager, SEEKCAMERA_IO_TYPE_USB);
    if (status != SEEKCAMERA_SUCCESS) {
        qCritical() << "Failed to create camera manager:" << status;
        return;
    }
    seekcamera_manager_register_event_callback(m_manager, &ThermalCamera::onEventCallback, this);
}

ThermalCamera::~ThermalCamera() {
    QMutexLocker lock(&m_mutex);
    if (m_manager) {
        seekcamera_manager_destroy(&m_manager);
        m_manager = nullptr;
        m_camera = nullptr;
    }
}

void ThermalCamera::setPalette(seekcamera_color_palette_t palette) {
    if (m_camera) {
        seekcamera_set_color_palette(m_camera, palette);
    }
}

void ThermalCamera::setPipeline(seekcamera_pipeline_mode_t mode) {
    if (m_camera) {
        seekcamera_set_pipeline_mode(m_camera, mode);
    }
}

void ThermalCamera::triggerShutter() {
    if (m_camera) {
        seekcamera_shutter_trigger(m_camera);
    }
}

void ThermalCamera::setEmissivity(float value) {
    if (value < 0.01f) value = 0.01f;
    if (value > 1.0f) value = 1.0f;

    QMutexLocker lock(&m_mutex);

    m_cachedEmissivity = value;

    if (m_camera) {
        seekcamera_set_scene_emissivity(m_camera, value);
        qInfo() << "Camera: Emissivity set to" << value;
    }

    emit EventBus::instance().emissivityChanged(value);
}

float ThermalCamera::getEmissivity() const {
    return m_cachedEmissivity;
}


void ThermalCamera::onEventCallback(seekcamera_t* cam, seekcamera_manager_event_t event, seekcamera_error_t status, void* user_data) {
    auto* self = static_cast<ThermalCamera*>(user_data);
    if (self) self->handleEvent(cam, event, status);
}

void ThermalCamera::handleEvent(seekcamera_t* cam, seekcamera_manager_event_t event, seekcamera_error_t status) {
    QMutexLocker lock(&m_mutex);

    switch (event) {
    case SEEKCAMERA_MANAGER_EVENT_CONNECT:
        m_camera = cam;
        seekcamera_set_scene_emissivity(m_camera, m_cachedEmissivity);
        seekcamera_set_pipeline_mode(m_camera, SEEKCAMERA_IMAGE_SEEKVISION);
        seekcamera_set_color_palette(m_camera, SEEKCAMERA_COLOR_PALETTE_SPECTRA);
        seekcamera_register_frame_available_callback(cam, &ThermalCamera::onFrameCallback, this);
        seekcamera_capture_session_start(cam, SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888 | SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT);

        seekcamera_chipid_t chipid;
        seekcamera_get_chipid(cam, &chipid);
        emit cameraConnected(QString::fromLatin1(chipid));
        break;

    case SEEKCAMERA_MANAGER_EVENT_DISCONNECT:
        m_camera = nullptr;
        emit cameraDisconnected("Device removed");
        break;

    case SEEKCAMERA_MANAGER_EVENT_ERROR:
        qWarning() << "Camera Error:" << status;
        emit cameraDisconnected("Internal Error");
        break;

    default: break;
    }
}

void ThermalCamera::onFrameCallback(seekcamera_t* cam, seekcamera_frame_t* frame, void* user_data) {
    Q_UNUSED(cam);
    auto* self = static_cast<ThermalCamera*>(user_data);
    if (self) self->handleFrame(frame);
}

void ThermalCamera::handleFrame(seekcamera_frame_t* frame) {
    if (seekcamera_frame_lock(frame) != SEEKCAMERA_SUCCESS) return;

    RawFrame output;

    // Process RGB data
    seekframe_t* rgbFrame = nullptr;
    seekcamera_frame_get_frame_by_format(frame, SEEKCAMERA_FRAME_FORMAT_COLOR_ARGB8888, &rgbFrame);

    if (rgbFrame) {
        output.w = seekframe_get_width(rgbFrame);
        output.h = seekframe_get_height(rgbFrame);

        size_t size = seekframe_get_data_size(rgbFrame);
        void* data = seekframe_get_data(rgbFrame);

        if (size > 0 && data) {
            output.pixelData.resize(size);
            memcpy(output.pixelData.data(), data, size);
        }
    }

    // Process Thermal Header
    seekframe_t* thermoFrame = nullptr;
    seekcamera_frame_get_frame_by_format(frame, SEEKCAMERA_FRAME_FORMAT_THERMOGRAPHY_FLOAT, &thermoFrame);

    if (thermoFrame) {
        auto* header = (seekcamera_frame_header_t*)seekframe_get_header(thermoFrame);

        if (header) {
            output.hot_spot.x = header->thermography_max_x;
            output.hot_spot.y = header->thermography_max_y;
            output.hot_spot.temperature = header->thermography_max_value;

            output.cold_spot.x = header->thermography_min_x;
            output.cold_spot.y = header->thermography_min_y;
            output.cold_spot.temperature = header->thermography_min_value;

            output.center_spot.x = header->thermography_spot_x;
            output.center_spot.y = header->thermography_spot_y;
            output.center_spot.temperature = header->thermography_spot_value;
        }
    }

    seekcamera_frame_unlock(frame);

    if (!output.pixelData.isEmpty()) {
        emit rawFrameReady(output);
    }
}
