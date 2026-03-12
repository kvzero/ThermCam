#include "storage_manager.h"
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QSocketNotifier>
#include <QDebug>

/* Low-level Linux headers for Netlink and VFS stats */
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <linux/netlink.h>
#include <unistd.h>
#include <cstring>

const QString StorageManager::kSdCardMountPoint = "/mnt/sdcard";
const QString StorageManager::kNandFallbackBase = "/root";
const QString StorageManager::kDcimSubdir       = "DCIM/ThermalCam";

StorageManager& StorageManager::instance() {
    static StorageManager inst;
    return inst;
}

StorageManager::StorageManager(QObject* parent) : QObject(parent) {
    m_quotaTimer = new QTimer(this);
    m_quotaTimer->setInterval(1000);
    connect(m_quotaTimer, &QTimer::timeout, this, &StorageManager::enforceActiveQuota);
}

StorageManager::~StorageManager() {
    if (m_netlinkFd >= 0) {
        ::close(m_netlinkFd);
    }
}

bool StorageManager::init() {
    struct sockaddr_nl nls;
    std::memset(&nls, 0, sizeof(struct sockaddr_nl));
    nls.nl_family = AF_NETLINK;
    nls.nl_pid = 0;
    nls.nl_groups = 1; // Listen to kernel multicast group 1 (KOBJECT_UEVENT)

    m_netlinkFd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (m_netlinkFd >= 0) {
        if (bind(m_netlinkFd, (struct sockaddr *)&nls, sizeof(struct sockaddr_nl)) == 0) {
            m_netlinkNotifier = new QSocketNotifier(m_netlinkFd, QSocketNotifier::Read, this);
            connect(m_netlinkNotifier, &QSocketNotifier::activated, this, &StorageManager::processNetlinkEvent);
            qInfo() << "[Storage] Block device UEVENT listener active.";
        } else {
            ::close(m_netlinkFd);
            m_netlinkFd = -1;
            qWarning() << "[Storage] Failed to bind Netlink socket.";
        }
    }

    // Initial check on startup
    evaluateSdCardState();
    return true;
}

bool StorageManager::isSdCardReady() const {
    return m_sdCardReady;
}

void StorageManager::setRecordingActive(bool active) {
    if (active) {
        m_quotaTimer->start();
    } else {
        m_quotaTimer->stop();
    }
}

QString StorageManager::requestMediaFilePath(CaptureMode mode) {
    QString targetBase;

    /**
     * Storage Routing Decision Tree
     * Primary Rule: Protect rootfs from high-bandwidth continuous writes.
     */
    if (m_sdCardReady) {
        // Path A: External Storage (Preferred)
        quint64 availableMB = getAvailableSpaceMB(kSdCardMountPoint);
        if (mode == CaptureMode::Video && availableMB < kMinRecordSpaceMB) {
            qWarning() << "[Storage] SD Card space insufficient for video. Available:" << availableMB << "MB";
            return QString();
        }
        targetBase = kSdCardMountPoint;
    } else {
        // Path B: Internal NAND Fallback
        // STRICT POLICY: Video is never allowed on NAND to prevent wear and capacity exhaustion.
        if (mode == CaptureMode::Video) {
            qWarning() << "[Storage] Denied: Video recording requires external SD Card.";
            return QString();
        }

        // Safety check: Ensure we don't brick the OS by filling rootfs
        quint64 availableMB = getAvailableSpaceMB(kNandFallbackBase);
        if (availableMB < kMinPhotoSpaceMB) {
            qWarning() << "[Storage] Denied: NAND critical space threshold reached.";
            return QString();
        }
        targetBase = kNandFallbackBase;
    }

    QString targetDir = targetBase + "/" + kDcimSubdir;
    if (!ensureDirectoryExists(targetDir)) {
        return QString();
    }

    return targetDir + "/" + generateTimestampFilename(mode);
}

QStringList StorageManager::getAvailableMediaDirectories() const {
    QStringList directories;

    if (m_sdCardReady) {
        QString sdPath = kSdCardMountPoint + "/" + kDcimSubdir;
        if (QDir(sdPath).exists()) {
            directories.append(sdPath);
        }
    }

    QString nandPath = kNandFallbackBase + "/" + kDcimSubdir;
    if (QDir(nandPath).exists()) {
        directories.append(nandPath);
    }

    return directories;
}

void StorageManager::processNetlinkEvent() {
    char buffer[4096];
    int len = ::recv(m_netlinkFd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (len <= 0) return;

    QString eventStr = QString::fromLatin1(buffer, len);
    // Filter for Block Device events related to MMC/SD
    if (eventStr.contains("mmcblk") || eventStr.contains("sd")) {
        /**
         * Wait for kernel automount scripts (e.g., mdev, udev) to finalize
         * the filesystem mounting process before assessing the directory.
         * Immediate checks often fail because the mount point isn't ready yet.
         */
        QTimer::singleShot(750, this, &StorageManager::evaluateSdCardState);
    }
}

void StorageManager::evaluateSdCardState() {
    bool previouslyReady = m_sdCardReady;

    // A card is "Ready" only if it is mounted AND has a readable filesystem
    m_sdCardReady = isMounted(kSdCardMountPoint) && (getAvailableSpaceMB(kSdCardMountPoint) > 0);

    if (m_sdCardReady && !previouslyReady) {
        // Auto-heal directory structure on insertion
        ensureDirectoryExists(kSdCardMountPoint + "/" + kDcimSubdir);
    }

    if (m_sdCardReady != previouslyReady) {
        qInfo() << "[Storage] SD Card state transitioned to:" << (m_sdCardReady ? "READY" : "REMOVED");
        emit sdCardStateChanged(m_sdCardReady);
    }
}

void StorageManager::enforceActiveQuota() {
    // This is called at 1Hz during video recording
    if (!m_sdCardReady) {
        // Card was yanked out during recording
        emit storageSpaceCritical();
        return;
    }

    quint64 availableMB = getAvailableSpaceMB(kSdCardMountPoint);
    if (availableMB <= kCriticalSpaceMB) {
        qCritical() << "[Storage] FATAL QUOTA REACHED during recording. Available:" << availableMB << "MB";
        emit storageSpaceCritical();
    }
}

bool StorageManager::isMounted(const QString& targetPath) const {
    QFile file("/proc/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    // Checking /proc/mounts is the most reliable way to verify kernel mount state
    QString content = QString::fromUtf8(file.readAll());
    return content.contains(targetPath);
}

quint64 StorageManager::getAvailableSpaceMB(const QString& path) const {
    struct statvfs stat;
    if (statvfs(path.toLocal8Bit().constData(), &stat) != 0) {
        return 0;
    }
    // Formula: (Available Blocks * Block Size) / (1024 * 1024)
    return (static_cast<quint64>(stat.f_bavail) * stat.f_frsize) / (1024 * 1024);
}

bool StorageManager::ensureDirectoryExists(const QString& path) const {
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qCritical() << "[Storage] Failed to create directory structure:" << path;
            return false;
        }
    }
    return true;
}

QString StorageManager::generateTimestampFilename(CaptureMode mode) const {
    // Adheres to simplified DCF-like naming with timestamps to avoid index collision
    QString prefix = (mode == CaptureMode::Photo) ? "IMG_" : "VID_";
    QString ext    = (mode == CaptureMode::Photo) ? ".jpg" : ".avi";
    QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    return prefix + stamp + ext;
}
