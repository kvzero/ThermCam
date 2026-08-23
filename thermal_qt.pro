QT       += core gui concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += lrelease embed_translations

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
                core/app_translator.cpp \
                core/event_bus.cpp \
                core/settings_store.cpp \
                hardware/hardware_manager.cpp \
                hardware/imaging/thermal_camera.cpp \
                hardware/rga/rga_image.cpp \
                hardware/hmi/haptic_provider.cpp \
                hardware/platform/system_control.cpp \
                hardware/hmi/key_manager.cpp \
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
                services/settings_service.cpp \
                services/operation_service.cpp \
                services/auto_shutdown_controller.cpp \
                ui/app.cpp \
                ui/settings_catalog.cpp \
                ui/overlays/media_viewer.cpp \
                ui/views/camera_view.cpp \
                ui/views/gallery_view.cpp \
                ui/views/settings_view.cpp \
                ui/overlays/clock_modal.cpp \
                ui/overlays/modal_dialog.cpp \
                ui/overlays/bubble_dialog.cpp \
                ui/overlays/toast_manager.cpp \
                ui/overlays/transition_layer.cpp \
                ui/overlays/poweroff_overlay.cpp \
                ui/overlays/palette_selector.cpp \
                ui/widgets/gallery_topbar.cpp \
                ui/widgets/scroll_indicator.cpp \
                ui/widgets/thermal_marker.cpp \
                ui/widgets/status_bar.cpp \
                ui/widgets/capsule_button.cpp \
                ui/widgets/mode_selector.cpp \
                ui/widgets/settings_row.cpp \
                ui/widgets/video_controlbar.cpp \
                ui/widgets/viewer_topbar.cpp

HEADERS     +=  core/global_context.h \
                core/app_translator.h \
                core/event_bus.h \
                core/types.h \
                core/settings_store.h \
                hardware/hardware_manager.h \
                hardware/imaging/thermal_camera.h \
                hardware/rga/rga_buffer.h \
                hardware/rga/rga_image.h \
                hardware/hmi/haptic_provider.h \
                hardware/platform/system_control.h \
                hardware/hmi/key_manager.h \
                hardware/sensor/battery_monitor.h \
                hardware/storage/storage_manager.h \
                processing/thermal_processor.h \
                processing/thermal_palette.h \
                media/image_decoder.h \
                media/image_encoder.h \
                media/video_encoder.h \
                media/video_player.h \
                media/video_prober.h \
                services/capture_service.h \
                services/capture_worker.h \
                services/gallery_service.h \
                services/gallery_worker.h \
                services/settings_service.h \
                services/operation_service.h \
                services/auto_shutdown_controller.h \
                ui/app.h \
                ui/settings_catalog.h \
                ui/overlays/media_viewer.h \
                ui/views/base_view.h \
                ui/views/camera_view.h \
                ui/views/gallery_view.h \
                ui/views/settings_view.h \
                core/settings_types.h \
                ui/overlays/clock_modal.h \
                ui/overlays/modal_dialog.h \
                ui/overlays/bubble_dialog.h \
                ui/overlays/toast_manager.h \
                ui/overlays/transition_layer.h \
                ui/overlays/poweroff_overlay.h \
                ui/overlays/palette_selector.h \
                ui/widgets/gallery_topbar.h \
                ui/widgets/scroll_indicator.h \
                ui/widgets/thermal_marker.h \
                ui/widgets/status_bar.h \
                ui/widgets/capsule_button.h \
                ui/widgets/mode_selector.h \
                ui/widgets/settings_row.h \
                ui/widgets/video_controlbar.h \
                ui/widgets/viewer_topbar.h

INCLUDEPATH += . \
               hardware/imaging/seekcam/include \
               $$[QT_SYSROOT]/usr/include/libdrm

LIBS        +=  -L$$PWD/hardware/imaging/seekcam/lib -lseekcamera \
                -lrga -ldrm \
                -lavformat -lavcodec -lavutil -lswscale \
                -lasound

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

TRANSLATIONS += i18n/thermal_zh_CN.ts
