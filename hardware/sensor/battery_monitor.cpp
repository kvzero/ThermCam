#include "battery_monitor.h"
#include "core/event_bus.h"
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QSocketNotifier>

// Headers required for Linux Netlink socket communication
#include <sys/socket.h>
#include <linux/netlink.h>
#include <unistd.h>
#include <cstring>

namespace {
    /**
     * @brief Identifies if a sysfs path belongs to the Charger IC.
     * Based on the driver name "bq2589...".
     */
    bool isCharger(const QString& path) {
        return path.contains("bq2589");
    }

    /**
     * @brief Identifies if a sysfs path belongs to the Fuel Gauge IC.
     * Based on the driver name "bq27...".
     */
    bool isGauge(const QString& path) {
        return path.contains("bq27");
    }
}

BatteryMonitor::BatteryMonitor(QObject *parent) : QObject(parent) {
    // Force the first update to trigger a change signal
    m_cachedInfo.status.level = -1;

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &BatteryMonitor::onPollTimeout);
}

BatteryMonitor::~BatteryMonitor() {
    if (m_netlinkFd >= 0) ::close(m_netlinkFd);
}

bool BatteryMonitor::init() {
    scanSysfsPaths();

    // 1. Setup Netlink Socket to listen for Kernel UEVENTs.
    // This allows the application to react immediately to hardware interrupts
    // (like plugging in a USB cable) without waiting for the next poll cycle.
    struct sockaddr_nl nls;
    memset(&nls, 0, sizeof(struct sockaddr_nl));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid = 0;
    nls.nl_groups = 1;

    m_netlinkFd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (m_netlinkFd >= 0) {
        if (bind(m_netlinkFd, (struct sockaddr *)&nls, sizeof(struct sockaddr_nl)) < 0)  {
            qWarning() << "[Battery] Failed to bind Netlink socket";
            ::close(m_netlinkFd);
            m_netlinkFd = -1;
        } else {
            m_netlinkNotifier = new QSocketNotifier(m_netlinkFd, QSocketNotifier::Read, this);
            connect(m_netlinkNotifier, &QSocketNotifier::activated, this, &BatteryMonitor::onNetlinkActivated);
            qInfo() << "[Battery] Netlink IRQ listener active";
        }
    } else {
        qWarning() << "[Battery] Netlink bind failed:" << strerror(errno);
    }

    // 2. Start the heartbeat timer (1Hz).
    // Provides continuous updates for analog values (Current/Voltage) and
    // serves as a fallback if UEVENTs are missed.
    m_pollTimer->start(kPollIntervalMs);

    // 3. Perform initial read to populate cache immediately.
    updateAll();

    return (!m_pathCharger.isEmpty() || !m_pathGauge.isEmpty());
}

void BatteryMonitor::scanSysfsPaths() {
    QDir dir(kSysfsRoot);
    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& entry : entries) {
        QString fullPath = dir.filePath(entry);
        if (isCharger(fullPath)) {
            m_pathCharger = fullPath;
            qInfo() << "[Battery] Found Charger:" << m_pathCharger;
        } else if (isGauge(fullPath)) {
            m_pathGauge = fullPath;
            qInfo() << "[Battery] Found Gauge:" << m_pathGauge;
        }
    }
}

BatteryInfo BatteryMonitor::getBatteryInfo() const {
    // Return the cached structure directly (Zero I/O latency).
    return m_cachedInfo;
}

// --- Event Handlers ---

void BatteryMonitor::onNetlinkActivated() {
    // Flush the socket buffer to clear the event signal.
    char buffer[4096];
    ::recv(m_netlinkFd, buffer, sizeof(buffer), MSG_DONTWAIT);

    // Immediate update on interrupt.
    updateAll();
}

void BatteryMonitor::onPollTimeout() {
    updateAll();
}

// --- Core Logic ---

void BatteryMonitor::updateAll() {
    BatteryInfo newData;

    /* Phase 1: Identify External Power Source (BQ25892) */
    if (!m_pathCharger.isEmpty()) {
        newData.status.isChargerConnected = (readInt(m_pathCharger, "online") == 1);
        const QString statusStr = readString(m_pathCharger, "status");

        /* Green UI is only shown if current is actively flowing into the battery */
        newData.status.isCharging = (statusStr == "Charging");
    }

    /* Phase 2: Evaluate Battery Presence and Telemetry (BQ27441) */
    if (!m_pathGauge.isEmpty()) {
        newData.status.isPresent = (readInt(m_pathGauge, "present") == 1);

        if (newData.status.isPresent) {
            newData.status.level = readInt(m_pathGauge, "capacity", 0);

            /* Read telemetry with micro-to-milli unit conversion */
            newData.voltage_mV = readInt(m_pathGauge, "voltage_now") / 1000;
            newData.current_mA = readInt(m_pathGauge, "current_now") / 1000;
            newData.full_capacity_mAh = readInt(m_pathGauge, "charge_full") / 1000;
            newData.design_capacity_mAh = readInt(m_pathGauge, "charge_full_design") / 1000;
            newData.health = readString(m_pathGauge, "health");

            if (QFile::exists(m_pathGauge + "/cycle_count")) {
                newData.cycle_count = readInt(m_pathGauge, "cycle_count", -1);
            }
        } else {
            /* Clear metrics if battery is physically absent */
            newData.status.level = 0;
            newData.voltage_mV = 0;
            newData.current_mA = 0;
            newData.health = "Absent";
        }
    } else {
        newData.status.level = 50;
    }

    /* Push lightweight update to EventBus only if UI-relevant states differ */
    if (newData.status != m_cachedInfo.status) {
        emit EventBus::instance().powerStatusChanged(newData.status);
    }

    /* Update heavyweight cache for pull-based telemetry */
    m_cachedInfo = newData;
}

// --- Sysfs IO Helpers ---

int BatteryMonitor::readInt(const QString& basePath, const QString& node, int defaultVal) const {
    QFile f(basePath + "/" + node);
    if (f.open(QIODevice::ReadOnly)) {
        bool ok;
        // Trim whitespace to avoid parsing errors
        int val = f.readAll().trimmed().toInt(&ok);
        return ok ? val : defaultVal;
    }
    return defaultVal;
}

QString BatteryMonitor::readString(const QString& basePath, const QString& node) const {
    QFile f(basePath + "/" + node);
    if (f.open(QIODevice::ReadOnly)) {
        return QString::fromUtf8(f.readAll()).trimmed();
    }
    return QString();
}
