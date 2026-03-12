#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <QObject>

/* Forward declarations to minimize header dependencies */
class ThermalCamera;
class InputManager;
class HapticProvider;
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
     * @brief Initialize all hardware components in correct order.
     * @return true if critical hardware (Camera/RGA) is ready.
     */
    bool init();

    /* Component Accessors */
    ThermalCamera* camera() const { return m_camera; }
    InputManager* input()   const { return m_input; }
    HapticProvider* haptic() const { return m_haptic; }
    BatteryMonitor* battery() const { return m_battery; }
    StorageManager* storage() const { return m_storage; }

private:
    explicit HardwareManager(QObject* parent = nullptr);
    ~HardwareManager() = default;

    HardwareManager(const HardwareManager&) = delete;
    HardwareManager& operator=(const HardwareManager&) = delete;

    ThermalCamera*  m_camera = nullptr;
    InputManager*   m_input  = nullptr;
    HapticProvider* m_haptic = nullptr;
    BatteryMonitor* m_battery = nullptr;
    StorageManager* m_storage = nullptr;
};

#endif // HARDWARE_MANAGER_H
