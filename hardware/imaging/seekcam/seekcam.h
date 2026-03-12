#ifndef SEEKCAM_H
#define SEEKCAM_H

#include <QObject>
#include <QMutex>
#include "seekcamera/seekcamera.h"
#include "seekcamera/seekcamera_manager.h"
#include "seekframe/seekframe.h"
#include "core/types.h"

class ThermalCamera : public QObject {
    Q_OBJECT
public:
    explicit ThermalCamera(QObject *parent = nullptr);
    ~ThermalCamera();

    void setPalette(seekcamera_color_palette_t palette);
    void setPipeline(seekcamera_pipeline_mode_t mode);
    void setEmissivity(float value);
    float getEmissivity() const;
    void triggerShutter();

signals:
    void cameraConnected(QString serial);
    void cameraDisconnected(QString reason);
    void rawFrameReady(const RawFrame& frame);

private:
    // Seek SDK Trampolines
    static void onEventCallback(seekcamera_t* cam, seekcamera_manager_event_t event, seekcamera_error_t status, void* user_data);
    static void onFrameCallback(seekcamera_t* cam, seekcamera_frame_t* frame, void* user_data);

    void handleEvent(seekcamera_t* cam, seekcamera_manager_event_t event, seekcamera_error_t status);
    void handleFrame(seekcamera_frame_t* frame);

    seekcamera_manager_t* m_manager = nullptr;
    seekcamera_t* m_camera = nullptr;

    QMutex m_mutex;

    float m_cachedEmissivity = 0.95f;
};

#endif // SEEKCAM_H
