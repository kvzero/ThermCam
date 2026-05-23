#include "storage_manager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QSocketNotifier>

/* Low-level Linux headers for Netlink and VFS stats */
#include <cstring>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

const QString StorageManager::kSdCardMountPoint = "/mnt/sdcard";
const QString StorageManager::kNandFallbackBase = "/root";
const QString StorageManager::kDcimSubdir       = "DCIM/ThermalCam";
const QStringList StorageManager::kUsbMountPointCandidates = {
    "/mnt/udisk",
    "/mnt/usb",
    "/mnt/usb0",
    "/mnt/usb1",
    "/media/udisk",
    "/media/usb",
    "/run/media/udisk"
};

namespace {
QString volumeName(StorageVolume volume) {
    switch (volume) {
    case StorageVolume::SdCard:
        return "SD Card";
    case StorageVolume::UsbDisk:
        return "USB Disk";
    case StorageVolume::Nand:
        return "NAND";
    }
    return "Unknown";
}
}

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
        if (bind(m_netlinkFd, reinterpret_cast<struct sockaddr*>(&nls), sizeof(struct sockaddr_nl)) == 0) {
            m_netlinkNotifier = new QSocketNotifier(m_netlinkFd, QSocketNotifier::Read, this);
            connect(m_netlinkNotifier, &QSocketNotifier::activated, this, &StorageManager::processNetlinkEvent);
            qInfo() << "[Storage] Block device UEVENT listener active.";
        } else {
            ::close(m_netlinkFd);
            m_netlinkFd = -1;
            qWarning() << "[Storage] Failed to bind Netlink socket.";
        }
    }

    evaluateStorageState();
    return true;
}

bool StorageManager::isSdCardReady() const {
    return m_sdCardStatus.ready;
}

bool StorageManager::isUsbDiskReady() const {
    return m_usbDiskStatus.ready;
}

StorageVolumeStatus StorageManager::volumeStatus(StorageVolume volume) const {
    return statusForVolume(volume);
}

StorageRoutingPolicy StorageManager::routingPolicy() const {
    return m_routingPolicy;
}

bool StorageManager::setRoutingPolicy(const StorageRoutingPolicy& policy, QString* outError) {
    const bool validPhoto =
        (policy.photoPriority == RemovableStoragePriority::SdFirst) ||
        (policy.photoPriority == RemovableStoragePriority::UsbFirst);
    const bool validVideo =
        (policy.videoPriority == RemovableStoragePriority::SdFirst) ||
        (policy.videoPriority == RemovableStoragePriority::UsbFirst);

    if (!validPhoto || !validVideo) {
        if (outError) *outError = "Unsupported removable priority value";
        return false;
    }

    m_routingPolicy = policy;
    return true;
}

void StorageManager::setRecordingActive(bool active) {
    m_recordingQuotaActive = active;

    if (active) {
        m_quotaTimer->start();
    } else {
        m_quotaTimer->stop();
        m_hasActiveRecordingVolume = false;
    }
}

QString StorageManager::requestMediaFilePath(CaptureMode mode) {
    evaluateStorageState();

    if (mode == CaptureMode::Video) {
        m_hasActiveRecordingVolume = false;
    }

    QString targetBase;
    StorageVolume selectedVolume = StorageVolume::SdCard;
    bool resolved = false;

    for (StorageVolume volume : removableOrderForMode(mode)) {
        quint64 availableMB = 0;
        if (!isVolumeWritableForMode(volume, mode, &availableMB)) {
            continue;
        }

        targetBase = basePathForVolume(volume);
        selectedVolume = volume;
        resolved = true;
        break;
    }

    if (!resolved && mode == CaptureMode::Photo && m_routingPolicy.allowNandPhotoFallback) {
        quint64 availableMB = 0;
        if (isVolumeWritableForMode(StorageVolume::Nand, mode, &availableMB)) {
            targetBase = basePathForVolume(StorageVolume::Nand);
            selectedVolume = StorageVolume::Nand;
            resolved = true;
        }
    }

    if (!resolved) {
        qWarning() << "[Storage] Denied: no writable target for mode"
                   << (mode == CaptureMode::Photo ? "PHOTO" : "VIDEO");
        return QString();
    }

    const QString targetDir = targetBase + "/" + kDcimSubdir;
    if (!ensureDirectoryExists(targetDir)) {
        return QString();
    }

    if (mode == CaptureMode::Video) {
        m_activeRecordingVolume = selectedVolume;
        m_hasActiveRecordingVolume = true;
    }

    return targetDir + "/" + generateTimestampFilename(mode);
}

QStringList StorageManager::getAvailableMediaDirectories() {
    evaluateStorageState();

    QStringList directories;

    const StorageVolumeStatus sd = m_sdCardStatus;
    if (sd.ready) {
        const QString sdPath = sd.mountPoint + "/" + kDcimSubdir;
        if (QDir(sdPath).exists()) {
            directories.append(sdPath);
        }
    }

    const StorageVolumeStatus usb = m_usbDiskStatus;
    if (usb.ready) {
        const QString usbPath = usb.mountPoint + "/" + kDcimSubdir;
        if (QDir(usbPath).exists() && !directories.contains(usbPath)) {
            directories.append(usbPath);
        }
    }

    const QString nandPath = kNandFallbackBase + "/" + kDcimSubdir;
    if (QDir(nandPath).exists() && !directories.contains(nandPath)) {
        directories.append(nandPath);
    }

    return directories;
}

void StorageManager::processNetlinkEvent() {
    char buffer[4096];
    const int len = ::recv(m_netlinkFd, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (len <= 0) return;

    const QString eventStr = QString::fromLatin1(buffer, len);
    const bool mightAffectStorage =
        eventStr.contains("mmcblk") ||
        eventStr.contains("usb") ||
        eventStr.contains("sd");

    if (mightAffectStorage) {
        QTimer::singleShot(kNetlinkSettleDelayMs, this, &StorageManager::evaluateStorageState);
    }
}

void StorageManager::evaluateStorageState() {
    const bool previousSdReady = m_sdCardStatus.ready;
    const bool previousUsbReady = m_usbDiskStatus.ready;
    const QString previousUsbMountPoint = m_usbDiskStatus.mountPoint;

    m_sdCardStatus = buildMountedVolumeStatus(kSdCardMountPoint);

    const QString usbMountPoint = resolveUsbMountPoint();
    if (usbMountPoint.isEmpty()) {
        m_usbDiskStatus = StorageVolumeStatus();
    } else {
        m_usbDiskStatus = buildMountedVolumeStatus(usbMountPoint);
    }

    if (m_sdCardStatus.ready && !previousSdReady) {
        ensureDirectoryExists(m_sdCardStatus.mountPoint + "/" + kDcimSubdir);
    }

    if (m_usbDiskStatus.ready && (!previousUsbReady || previousUsbMountPoint != m_usbDiskStatus.mountPoint)) {
        ensureDirectoryExists(m_usbDiskStatus.mountPoint + "/" + kDcimSubdir);
    }

    if (m_sdCardStatus.ready != previousSdReady) {
        qInfo() << "[Storage] SD Card state transitioned to:"
                << (m_sdCardStatus.ready ? "READY" : "REMOVED");
        emit sdCardStateChanged(m_sdCardStatus.ready);
    }

    if (m_usbDiskStatus.ready != previousUsbReady) {
        qInfo() << "[Storage] USB Disk state transitioned to:"
                << (m_usbDiskStatus.ready ? "READY" : "REMOVED");
        emit usbDiskStateChanged(m_usbDiskStatus.ready);
    }
}

void StorageManager::enforceActiveQuota() {
    if (!m_recordingQuotaActive) return;

    evaluateStorageState();

    if (!m_hasActiveRecordingVolume) {
        emit storageSpaceCritical();
        return;
    }

    const StorageVolumeStatus recordingVolume = statusForVolume(m_activeRecordingVolume);
    if (!recordingVolume.ready) {
        qWarning() << "[Storage] Recording target disappeared:" << volumeName(m_activeRecordingVolume);
        emit storageSpaceCritical();
        return;
    }

    if (recordingVolume.availableMB <= kCriticalSpaceMB) {
        qCritical() << "[Storage] FATAL QUOTA REACHED during recording."
                    << volumeName(m_activeRecordingVolume)
                    << "available:" << recordingVolume.availableMB << "MB";
        emit storageSpaceCritical();
    }
}

bool StorageManager::isMounted(const QString& targetPath) const {
    QFile file("/proc/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    return content.contains(targetPath);
}

quint64 StorageManager::getAvailableSpaceMB(const QString& path) const {
    struct statvfs stat;
    if (statvfs(path.toLocal8Bit().constData(), &stat) != 0) {
        return 0;
    }
    return (static_cast<quint64>(stat.f_bavail) * stat.f_frsize) / (1024 * 1024);
}

quint64 StorageManager::getTotalSpaceMB(const QString& path) const {
    struct statvfs stat;
    if (statvfs(path.toLocal8Bit().constData(), &stat) != 0) {
        return 0;
    }
    return (static_cast<quint64>(stat.f_blocks) * stat.f_frsize) / (1024 * 1024);
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
    const QString prefix = (mode == CaptureMode::Photo) ? "IMG_" : "VID_";
    const QString ext    = (mode == CaptureMode::Photo) ? ".jpg" : ".avi";
    const QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return prefix + stamp + ext;
}

StorageVolumeStatus StorageManager::buildMountedVolumeStatus(const QString& mountPoint) const {
    StorageVolumeStatus status;
    status.mountPoint = mountPoint;

    if (mountPoint.isEmpty()) return status;
    if (!isMounted(mountPoint)) return status;

    // "Ready" means mounted/present; capacity thresholds are evaluated by routing policy.
    status.ready = true;
    status.availableMB = getAvailableSpaceMB(mountPoint);
    status.totalMB = getTotalSpaceMB(mountPoint);
    return status;
}

StorageVolumeStatus StorageManager::buildNandStatus() const {
    StorageVolumeStatus status;
    status.mountPoint = kNandFallbackBase;

    if (!QDir(kNandFallbackBase).exists()) {
        return status;
    }

    status.availableMB = getAvailableSpaceMB(kNandFallbackBase);
    status.totalMB = getTotalSpaceMB(kNandFallbackBase);
    status.ready = (status.availableMB > 0);
    return status;
}

QString StorageManager::resolveUsbMountPoint() const {
    QSet<QString> candidateMounts;
    for (const QString& mountPoint : kUsbMountPointCandidates) {
        candidateMounts.insert(mountPoint);
    }

    QFile mounts("/proc/mounts");
    if (mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!mounts.atEnd()) {
            const QString line = QString::fromUtf8(mounts.readLine()).trimmed();
            if (line.isEmpty()) continue;

            const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
            if (fields.size() < 2) continue;

            const QString device = fields.at(0);
            const QString mountPoint = fields.at(1);

            if (!device.startsWith("/dev/sd")) continue;
            if (mountPoint == "/") continue;
            if (mountPoint.startsWith("/boot")) continue;
            if (mountPoint == kSdCardMountPoint) continue;

            candidateMounts.insert(mountPoint);
        }
    }

    QString bestMountPoint;
    quint64 bestAvailableMB = 0;
    for (const QString& mountPoint : candidateMounts) {
        const StorageVolumeStatus status = buildMountedVolumeStatus(mountPoint);
        if (!status.ready) continue;

        if (bestMountPoint.isEmpty()) {
            bestMountPoint = status.mountPoint;
            bestAvailableMB = status.availableMB;
            continue;
        }

        if (status.availableMB > bestAvailableMB) {
            bestAvailableMB = status.availableMB;
            bestMountPoint = status.mountPoint;
        }
    }
    return bestMountPoint;
}

QVector<StorageVolume> StorageManager::removableOrderForMode(CaptureMode mode) const {
    const RemovableStoragePriority priority =
        (mode == CaptureMode::Photo) ? m_routingPolicy.photoPriority : m_routingPolicy.videoPriority;

    if (priority == RemovableStoragePriority::UsbFirst) {
        return {StorageVolume::UsbDisk, StorageVolume::SdCard};
    }
    return {StorageVolume::SdCard, StorageVolume::UsbDisk};
}

StorageVolumeStatus StorageManager::statusForVolume(StorageVolume volume) const {
    switch (volume) {
    case StorageVolume::SdCard:
        return m_sdCardStatus;
    case StorageVolume::UsbDisk:
        return m_usbDiskStatus;
    case StorageVolume::Nand:
        return buildNandStatus();
    }
    return StorageVolumeStatus();
}

bool StorageManager::isVolumeWritableForMode(StorageVolume volume, CaptureMode mode, quint64* outAvailableMB) const {
    if (mode == CaptureMode::Video && volume == StorageVolume::Nand) {
        qWarning() << "[Storage] Denied: Video recording requires removable media (SD/USB).";
        return false;
    }

    const StorageVolumeStatus status = statusForVolume(volume);
    if (!status.ready) {
        return false;
    }

    const quint64 availableMB = status.availableMB;
    if (outAvailableMB) {
        *outAvailableMB = availableMB;
    }

    if (mode == CaptureMode::Video) {
        if (availableMB < kMinRecordSpaceMB) {
            qWarning() << "[Storage]" << volumeName(volume)
                       << "space insufficient for video. Available:" << availableMB << "MB";
            return false;
        }
        return true;
    }

    if (availableMB < kMinPhotoSpaceMB) {
        qWarning() << "[Storage]" << volumeName(volume)
                   << "space insufficient for photo. Available:" << availableMB << "MB";
        return false;
    }
    return true;
}

QString StorageManager::basePathForVolume(StorageVolume volume) const {
    switch (volume) {
    case StorageVolume::SdCard:
        return m_sdCardStatus.mountPoint.isEmpty() ? kSdCardMountPoint : m_sdCardStatus.mountPoint;
    case StorageVolume::UsbDisk:
        return m_usbDiskStatus.mountPoint;
    case StorageVolume::Nand:
        return kNandFallbackBase;
    }
    return QString();
}
