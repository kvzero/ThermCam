#include "storage_manager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSocketNotifier>

/* Low-level Linux headers for Netlink and VFS stats */
#include <cstring>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

const QString StorageManager::kSdCardMountPoint = "/mnt/sdcard";
const QString StorageManager::kUsbDiskMountPoint = "/mnt/udisk";
const QString StorageManager::kNandFallbackBase = "/root";
const QString StorageManager::kDcimSubdir       = "DCIM/ThermalCam";

/* ========================= File-Local Constants ========================= */
namespace {
constexpr quint64 kFat32BoundaryMB = 32ull * 1024ull;
constexpr int kFormatCommandTimeoutMs = 180000;
const QString kRemovableMountOptions = "noexec,nodev,noatime,nodiratime";

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

/* ========================= Lifecycle / Bootstrap ========================= */
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

/* ========================= Public Status API ========================= */
bool StorageManager::isSdCardReady() const {
    return m_sdCardStatus.ready;
}

bool StorageManager::isUsbDiskReady() const {
    return m_usbDiskStatus.ready;
}

/**
 * @brief Format one removable medium according to capacity policy and remount it.
 *
 * Policy:
 * - <= 32 GiB -> FAT32
 * - >  32 GiB -> exFAT
 */
bool StorageManager::formatVolume(StorageVolume volume, QString* outError) {
    if (volume == StorageVolume::Nand) {
        if (outError) *outError = "NAND format is not supported";
        return false;
    }

    if (m_recordingQuotaActive) {
        if (outError) *outError = "Formatting is blocked while recording";
        return false;
    }

    evaluateStorageState();

    const QString mountPoint =
        (volume == StorageVolume::SdCard) ? kSdCardMountPoint : kUsbDiskMountPoint;
    const StorageVolumeStatus status = statusForVolume(volume);
    if (!status.ready) {
        if (outError) *outError = QString("%1 is not mounted").arg(volumeName(volume));
        return false;
    }

    const QString deviceNode = mountedDeviceForPath(mountPoint);
    if (deviceNode.isEmpty()) {
        if (outError) *outError = QString("Cannot resolve block device for %1").arg(mountPoint);
        return false;
    }

    if (!deviceNode.startsWith("/dev/")) {
        if (outError) *outError = QString("Resolved device is invalid: %1").arg(deviceNode);
        return false;
    }

    if (volume == StorageVolume::SdCard && !deviceNode.startsWith("/dev/mmcblk")) {
        if (outError) *outError = QString("Unexpected SD device node: %1").arg(deviceNode);
        return false;
    }

    if (volume == StorageVolume::UsbDisk && !deviceNode.startsWith("/dev/sd")) {
        if (outError) *outError = QString("Unexpected USB device node: %1").arg(deviceNode);
        return false;
    }

    quint64 totalMB = status.totalMB;
    if (totalMB == 0) {
        totalMB = getTotalSpaceMB(mountPoint);
    }
    if (totalMB == 0) {
        if (outError) *outError = QString("Cannot detect capacity for %1").arg(volumeName(volume));
        return false;
    }

    const bool useFat32 = (totalMB <= kFat32BoundaryMB);
    const QString fsType = useFat32 ? "vfat" : "exfat";
    const QString mkfsProgram = useFat32 ? "/usr/sbin/mkfs.vfat" : "/usr/sbin/mkfs.exfat";
    const QStringList mkfsArgs = useFat32
                                     ? QStringList({"-F", "32", deviceNode})
                                     : QStringList({deviceNode});

    qInfo() << "[Storage] Formatting" << volumeName(volume)
            << "mountPoint:" << mountPoint
            << "device:" << deviceNode
            << "sizeMB:" << totalMB
            << "targetFs:" << fsType;

    ::sync();

    QString commandError;
    if (!runCommand("/usr/bin/umount", {"-l", mountPoint}, &commandError, kFormatCommandTimeoutMs)) {
        evaluateStorageState();
        if (outError) *outError = QString("umount failed: %1").arg(commandError);
        return false;
    }

    if (!runCommand(mkfsProgram, mkfsArgs, &commandError, kFormatCommandTimeoutMs)) {
        evaluateStorageState();
        if (outError) *outError = QString("mkfs failed: %1").arg(commandError);
        return false;
    }

    if (!ensureDirectoryExists(mountPoint)) {
        if (outError) *outError = QString("Mount point missing: %1").arg(mountPoint);
        return false;
    }

    if (!runCommand("/usr/bin/mount",
                    {"-t", fsType, "-o", kRemovableMountOptions, deviceNode, mountPoint},
                    &commandError,
                    kFormatCommandTimeoutMs)) {
        evaluateStorageState();
        if (outError) *outError = QString("mount failed: %1").arg(commandError);
        return false;
    }

    ::sync();
    evaluateStorageState();
    return true;
}

/* ========================= Routing / Policy API ========================= */
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

/* ========================= Capture Session Hooks ========================= */
void StorageManager::setRecordingActive(bool active) {
    m_recordingQuotaActive = active;

    if (active) {
        m_quotaTimer->start();
    } else {
        m_quotaTimer->stop();
        m_hasActiveRecordingVolume = false;
    }
}

/* ========================= Capture / Gallery Path API ========================= */
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

/* ========================= Netlink + State Refresh ========================= */
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

/**
 * @brief Refreshes SD/USB mounted status and emits transition signals.
 */
void StorageManager::evaluateStorageState() {
    const bool previousSdReady = m_sdCardStatus.ready;
    const bool previousUsbReady = m_usbDiskStatus.ready;

    m_sdCardStatus = buildMountedVolumeStatus(kSdCardMountPoint);
    m_usbDiskStatus = buildMountedVolumeStatus(kUsbDiskMountPoint);

    if (m_sdCardStatus.ready && !previousSdReady) {
        ensureDirectoryExists(m_sdCardStatus.mountPoint + "/" + kDcimSubdir);
    }

    if (m_usbDiskStatus.ready && !previousUsbReady) {
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

/* ========================= Quota Enforcement ========================= */
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

/* ========================= Mount Table Helpers ========================= */
/**
 * @brief Checks whether @p targetPath is an active mount point in /proc/mounts.
 */
bool StorageManager::isMounted(const QString& targetPath) const {
    QFile file("/proc/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    // !!! Do not use QFile::atEnd() as loop condition for procfs streams.
    // On this target, /proc/mounts may report EOF before the first read.
    // Read line-by-line until readLine() returns a null QByteArray instead.
    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isNull()) {
            break; // EOF on procfs stream
        }

        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() >= 2 && fields.at(1) == targetPath) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Resolves mounted source device node for @p targetPath from /proc/self/mounts.
 */
QString StorageManager::mountedDeviceForPath(const QString& targetPath) const {
    QFile file("/proc/self/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isNull()) {
            break;
        }

        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() >= 2 && fields.at(1) == targetPath) {
            return fields.at(0);
        }
    }

    return QString();
}

/* ========================= Process Execution Helper ========================= */
/**
 * @brief Executes one external command with timeout/error handling.
 */
bool StorageManager::runCommand(const QString& program,
                                const QStringList& args,
                                QString* outError,
                                int timeoutMs) const {
    QProcess process;
    process.start(program, args, QIODevice::ReadOnly);

    if (!process.waitForStarted(5000)) {
        if (outError) {
            *outError = QString("failed to start %1").arg(program);
        }
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        if (outError) {
            *outError = QString("%1 timed out").arg(program);
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();

        if (outError) {
            if (!stderrText.isEmpty()) {
                *outError = stderrText;
            } else if (!stdoutText.isEmpty()) {
                *outError = stdoutText;
            } else {
                *outError = QString("%1 exited with code %2").arg(program).arg(process.exitCode());
            }
        }
        return false;
    }

    return true;
}

/* ========================= Capacity + Path Helpers ========================= */
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

/* ========================= Internal Status Builders ========================= */
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

/* ========================= Internal Routing Helpers ========================= */
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
        return m_usbDiskStatus.mountPoint.isEmpty() ? kUsbDiskMountPoint : m_usbDiskStatus.mountPoint;
    case StorageVolume::Nand:
        return kNandFallbackBase;
    }
    return QString();
}
