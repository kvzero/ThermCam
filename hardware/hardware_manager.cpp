#include "core/event_bus.h"
#include "hardware_manager.h"
#include "hardware/rga/rga_image.h"
#include "imaging/thermal_camera.h"
#include "hardware/hmi/key_manager.h"
#include "hardware/hmi/haptic_provider.h"
#include "hardware/platform/system_control.h"
#include "hardware/sensor/battery_monitor.h"
#include "hardware/storage/storage_manager.h"
#include <QDebug>
#include <utility>

HardwareManager& HardwareManager::instance() {
    static HardwareManager inst;
    return inst;
}

HardwareManager::HardwareManager(QObject* parent) : QObject(parent) {}

HardwareManager::~HardwareManager() = default;

void HardwareManager::createCamera(ThermalCamera::StartupHandle startup) {
    m_camera = new ThermalCamera(std::move(startup), this);
}

bool HardwareManager::init() {
    qInfo() << "HardwareManager: Initializing subsystems...";

    RgaImage::globalInit();

    m_keys = new KeyManager(this);
    if (!m_keys->init()) {
        qWarning() << "HardwareManager: KeyManager failed";
    }

    m_haptic = &HapticProvider::instance();
    if (!m_haptic->init()) {
        qWarning() << "HardwareManager: HapticProvider failed";
    }

    m_systemControl = new SystemControl(this);
    if (!m_systemControl->init()) {
        qWarning() << "HardwareManager: SystemControl init failed";
    }

    m_battery = new BatteryMonitor(this);
    if (!m_battery->init()) {
        qWarning() << "HardwareManager: BatteryMonitor init failed";
    }

    m_storage = &StorageManager::instance();
    if (!m_storage->init()) {
        qWarning() << "HardwareManager: StorageManager init failed (Netlink error?)";
    }

    connect(&EventBus::instance(), &EventBus::hapticRequested,
            m_haptic, &HapticProvider::playEffect);

    qInfo() << "HardwareManager: Subsystems ready.";
    return true;
}
