#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <QObject>
#include <QTimer>
#include "core/types.h"

class QSocketNotifier;

/**
 * @brief Hardware Abstraction Layer (HAL) for the Power Subsystem.
 *
 * Supports BQ25892 (Charger) and BQ27441 (Fuel Gauge).
 * Implements a hybrid update mechanism:
 * 1. Interrupt-driven (Netlink): Immediate response to plug/unplug events.
 * 2. Timer-driven (1Hz): Periodic synchronization for analog values (Current/Voltage).
 */
class BatteryMonitor : public QObject {
    Q_OBJECT
public:
    explicit BatteryMonitor(QObject *parent = nullptr);
    ~BatteryMonitor();

    /**
     * @brief Initializes the subsystem by scanning sysfs and binding the Netlink socket.
     * @return true if critical drivers are found.
     */
    bool init();

    /**
     * @brief Thread-safe access to the latest full telemetry data.
     * This method reads from memory cache, avoiding blocking I/O on the UI thread.
     */
    BatteryInfo getBatteryInfo() const;

private slots:
    /**
     * @brief Triggered by Linux Kernel UEVENT (via Netlink).
     * Handles instant hardware state changes (e.g., cable insertion).
     */
    void onNetlinkActivated();

    /**
     * @brief Triggered by internal 1Hz timer.
     * Refreshes continuous data like current and voltage.
     */
    void onPollTimeout();

private:
    // --- Internal Logic ---
    void scanSysfsPaths();
    void updateAll(); // Reads all sysfs nodes, updates cache, and emits signals if changed.

    // Sysfs I/O Helpers
    int readInt(const QString& basePath, const QString& node, int defaultVal = 0) const;
    QString readString(const QString& basePath, const QString& node) const;

    // --- State Cache ---
    BatteryInfo m_cachedInfo;

    // --- Driver Paths ---
    QString m_pathCharger; // Path to Charger driver (BQ25892)
    QString m_pathGauge;   // Path to Fuel Gauge driver (BQ27441)

    // --- Event Mechanisms ---
    int m_netlinkFd = -1;
    QSocketNotifier* m_netlinkNotifier = nullptr;
    QTimer* m_pollTimer = nullptr;

    // --- Configuration Constants ---
    static constexpr int kPollIntervalMs = 1000;
    static constexpr const char* kSysfsRoot = "/sys/class/power_supply";
};

#endif // BATTERY_MONITOR_H
