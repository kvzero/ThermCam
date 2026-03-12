#include "core/event_bus.h"
#include "hardware_manager.h"
#include "hardware/rga/rga_image.h"
#include "imaging/seekcam/seekcam.h"
#include "hardware/hmi/input_manager.h"
#include "hardware/hmi/haptic_provider.h"
#include "hardware/sensor/battery_monitor.h"
#include "hardware/storage/storage_manager.h"
#include <QDebug>

HardwareManager& HardwareManager::instance() {
    static HardwareManager inst;
    return inst;
}

HardwareManager::HardwareManager(QObject* parent) : QObject(parent) {}

bool HardwareManager::init() {
    qInfo() << "HardwareManager: Initializing subsystems...";

    RgaImage::globalInit();

    m_input = new InputManager(this);
    if (!m_input->init()) {
        qWarning() << "HardwareManager: InputManager failed";
    }

    m_haptic = &HapticProvider::instance();
    if (!m_haptic->init()) {
        qWarning() << "HardwareManager: HapticProvider failed";
    }

    m_battery = new BatteryMonitor(this);
    if (!m_battery->init()) {
        qWarning() << "HardwareManager: BatteryMonitor init failed";
    }

    m_storage = &StorageManager::instance();
    if (!m_storage->init()) {
        qWarning() << "HardwareManager: StorageManager init failed (Netlink error?)";
    }

    m_camera = new ThermalCamera(this);
    // Note: Seek SDK might take some time to detect USB device,
    // we assume the object creation is enough to start its internal manager.

    connect(&EventBus::instance(), &EventBus::hapticRequested,
            m_haptic, &HapticProvider::playEffect);

    qInfo() << "HardwareManager: Subsystems ready.";
    return true;
}
