QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES     +=  main.cpp \
                core/global_context.cpp \
                core/event_bus.cpp \
                hardware/hardware_manager.cpp \
                hardware/imaging/seekcam/seekcam.cpp \
                hardware/rga/rga_image.cpp \
                hardware/hmi/haptic_provider.cpp \
                hardware/hmi/input_manager.cpp \
                hardware/sensor/battery_monitor.cpp \
                hardware/storage/storage_manager.cpp \
                processing/thermal_processor.cpp \
                media/image_encoder.cpp \
                media/image_decoder.cpp \
                media/video_encoder.cpp \
                media/video_player.cpp \
                media/video_prober.cpp \
                services/capture_service.cpp \
                services/capture_worker.cpp \
                services/gallery_service.cpp \
                services/gallery_worker.cpp \
                ui/app.cpp \
                ui/gesture_recognizer.cpp \
                ui/overlays/media_viewer.cpp \
                ui/ui_controller.cpp \
                ui/views/camera_view.cpp \
                ui/views/gallery_view.cpp \
                ui/overlays/confirm_dialog.cpp \
                ui/overlays/toast_manager.cpp \
                ui/overlays/transition_layer.cpp \
                ui/widgets/gallery_topbar.cpp \
                ui/widgets/scroll_indicator.cpp \
                ui/widgets/thermal_marker.cpp \
                ui/widgets/hud_container.cpp \
                ui/widgets/status_bar.cpp \
                ui/widgets/capsule_button.cpp \
                ui/widgets/mode_selector.cpp \
                ui/widgets/video_controlbar.cpp \
                ui/widgets/viewer_topbar.cpp

HEADERS     +=  core/global_context.h \
                core/event_bus.h \
                core/types.h \
                hardware/hardware_manager.h \
                hardware/imaging/seekcam/seekcam.h \
                hardware/rga/rga_buffer.h \
                hardware/rga/rga_image.h \
                hardware/hmi/haptic_provider.h \
                hardware/hmi/input_manager.h \
                hardware/sensor/battery_monitor.h \
                hardware/storage/storage_manager.h \
                processing/thermal_processor.h \
                media/image_decoder.h \
                media/image_encoder.h \
                media/video_encoder.h \
                media/video_player.h \
                media/video_prober.h \
                services/capture_service.h \
                services/capture_worker.h \
                services/gallery_service.h \
                services/gallery_worker.h \
                ui/app.h \
                ui/gesture_recognizer.h \
                ui/overlays/media_viewer.h \
                ui/ui_controller.h \
                ui/views/base_view.h \
                ui/views/camera_view.h \
                ui/views/gallery_view.h \
                ui/overlays/confirm_dialog.h \
                ui/overlays/toast_manager.h \
                ui/overlays/transition_layer.h \
                ui/widgets/gallery_topbar.h \
                ui/widgets/scroll_indicator.h \
                ui/widgets/thermal_marker.h \
                ui/widgets/hud_container.h \
                ui/widgets/status_bar.h \
                ui/widgets/capsule_button.h \
                ui/widgets/mode_selector.h \
                ui/widgets/video_controlbar.h \
                ui/widgets/viewer_topbar.h

INCLUDEPATH += . \
               hardware/imaging/seekcam/include \
               $$[QT_SYSROOT]/usr/include/libdrm

LIBS        +=  -L$$PWD/hardware/imaging/seekcam/lib -lseekcamera \
                -lrga -ldrm \
                -lavformat -lavcodec -lavutil -lswscale

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target


