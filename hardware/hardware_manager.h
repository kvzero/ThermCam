#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <QObject>

#include "hardware/imaging/thermal_camera.h"

/* Forward declarations to minimize header dependencies */
class KeyManager;
class HapticProvider;
class SystemControl;
class BatteryMonitor;
class StorageManager;

/**
 * @brief Central coordinator for all hardware lifecycle and ownership.
 */
class HardwareManager : public QObject {
    Q_OBJECT
public:
    static HardwareManager& instance();

    /**
     * @brief Initialize HardwareManager-owned subsystems.
     * @return true after best-effort subsystem initialization completes.
     */
    bool init();
    void createCamera(ThermalCamera::StartupHandle startup);

    /* Component Accessors */
    ThermalCamera* camera() const { return m_camera; }
    KeyManager* keys() const { return m_keys; }
    HapticProvider* haptic() const { return m_haptic; }
    SystemControl* systemControl() const { return m_systemControl; }
    BatteryMonitor* battery() const { return m_battery; }
    StorageManager* storage() const { return m_storage; }

private:
    explicit HardwareManager(QObject* parent = nullptr);
    ~HardwareManager() override;

    HardwareManager(const HardwareManager&) = delete;
    HardwareManager& operator=(const HardwareManager&) = delete;

    ThermalCamera*  m_camera = nullptr;
    KeyManager*     m_keys = nullptr;
    HapticProvider* m_haptic = nullptr;
    SystemControl* m_systemControl = nullptr;
    BatteryMonitor* m_battery = nullptr;
    StorageManager* m_storage = nullptr;
};

#endif // HARDWARE_MANAGER_H
