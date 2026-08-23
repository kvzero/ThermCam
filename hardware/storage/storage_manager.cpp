#include "storage_manager.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QPointer>
#include <QSocketNotifier>
#include <QtConcurrent/QtConcurrentRun>

/* Low-level Linux headers for Netlink and VFS stats */
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

const QString StorageManager::kSdCardMountPoint = "/mnt/sdcard";
const QString StorageManager::kUsbDiskMountPoint = "/mnt/udisk";
const QString StorageManager::kNandMountPoint = "/userdata";
const QString StorageManager::kDcimSubdir       = "DCIM/ThermalCam";

/* ========================= File-Local Constants ========================= */
namespace {
constexpr quint64 kFat32BoundaryMB = 32ull * 1024ull;
constexpr int kFormatCommandTimeoutMs = 180000;
const QString kRemovableMountOptions = "noexec,nodev,noatime,nodiratime";

struct FormatPlan {
    StorageVolume volume = StorageVolume::SdCard;
    QString mountPoint;
    QString deviceNode;
    QString fileSystem;
    QString mkfsProgram;
    QStringList mkfsArguments;
};

bool usesFat32ForCapacity(quint64 capacityMB) {
    return capacityMB <= kFat32BoundaryMB;
}

QString volumeName(StorageVolume volume) {
    switch (volume) {
    case StorageVolume::SdCard:
        return "SD Card";
    case StorageVolume::UsbDisk:
        return "USB Disk";
    case StorageVolume::Nand:
        return "NAND";
    }
    Q_UNREACHABLE();
}

bool runProcessCommand(const QString& program,
                       const QStringList& arguments,
                       QString* outError,
                       int timeoutMs) {
    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);

    if (!process.waitForStarted(5000)) {
        if (outError) *outError = QString("failed to start %1").arg(program);
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        if (outError) *outError = QString("%1 timed out").arg(program);
        return false;
    }

    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }

    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (outError) {
        *outError = !stderrText.isEmpty() ? stderrText
            : (!stdoutText.isEmpty() ? stdoutText
                                     : QString("%1 exited with code %2")
                                           .arg(program).arg(process.exitCode()));
    }
    return false;
}

bool executeFormat(const FormatPlan& plan) {
    if (!runProcessCommand("/usr/bin/sync", {}, nullptr, kFormatCommandTimeoutMs)) {
        return false;
    }
    if (!runProcessCommand("/usr/bin/umount", {"-l", plan.mountPoint}, nullptr,
                           kFormatCommandTimeoutMs)) {
        return false;
    }
    if (!runProcessCommand(plan.mkfsProgram, plan.mkfsArguments, nullptr,
                           kFormatCommandTimeoutMs)) {
        return false;
    }
    if (!runProcessCommand("/usr/bin/mount",
                           {"-t", plan.fileSystem, "-o", kRemovableMountOptions,
                            plan.deviceNode, plan.mountPoint},
                           nullptr, kFormatCommandTimeoutMs)) {
        return false;
    }
    if (!runProcessCommand("/usr/bin/sync", {}, nullptr, kFormatCommandTimeoutMs)) {
        return false;
    }
    return true;
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
    if (m_formatFuture.isRunning()) {
        m_formatFuture.waitForFinished();
    }
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

QString StorageManager::formatFileSystemName(StorageVolume volume) {
    if (volume == StorageVolume::Nand) return {};

    evaluateStorageState();
    const quint64 totalMB = statusForVolume(volume).totalMB;
    if (totalMB == 0) return {};

    return usesFat32ForCapacity(totalMB) ? QStringLiteral("FAT32")
                                          : QStringLiteral("exFAT");
}

/**
 * @brief Format one removable medium according to capacity policy and remount it.
 *
 * Policy:
 * - <= 32 GiB -> FAT32
 * - >  32 GiB -> exFAT
 */
bool StorageManager::startFormatVolume(StorageVolume volume) {
    if (volume == StorageVolume::Nand) return false;
    if (m_recordingQuotaActive) return false;
    if (m_formatVolume.has_value()) return false;

    evaluateStorageState();

    const QString mountPoint =
        (volume == StorageVolume::SdCard) ? kSdCardMountPoint : kUsbDiskMountPoint;
    const StorageVolumeStatus status = statusForVolume(volume);
    if (!status.ready) return false;

    const QString deviceNode = mountedDeviceForPath(mountPoint);
    if (deviceNode.isEmpty() || !deviceNode.startsWith("/dev/")) return false;
    if (volume == StorageVolume::SdCard && !deviceNode.startsWith("/dev/mmcblk")) return false;
    if (volume == StorageVolume::UsbDisk && !deviceNode.startsWith("/dev/sd")) return false;

    quint64 totalMB = status.totalMB;
    if (totalMB == 0) {
        totalMB = getTotalSpaceMB(mountPoint);
    }
    if (totalMB == 0) return false;
    if (!ensureDirectoryExists(mountPoint)) return false;

    const bool useFat32 = usesFat32ForCapacity(totalMB);
    const FormatPlan plan = {
        volume,
        mountPoint,
        deviceNode,
        useFat32 ? QStringLiteral("vfat") : QStringLiteral("exfat"),
        useFat32 ? QStringLiteral("/usr/sbin/mkfs.vfat")
                 : QStringLiteral("/usr/sbin/mkfs.exfat"),
        useFat32 ? QStringList({"-F", "32", deviceNode}) : QStringList({deviceNode})
    };

    m_formatVolume = volume;
    if (volume == StorageVolume::SdCard) {
        m_sdCardStatus = {};
        emit sdCardStateChanged(false);
    } else {
        m_usbDiskStatus = {};
        emit usbDiskStateChanged(false);
    }

    const QPointer<StorageManager> self(this);
    m_formatFuture = QtConcurrent::run([self, plan]() {
        const bool success = executeFormat(plan);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, plan, success]() {
            if (!self) return;
            self->m_formatVolume.reset();
            self->evaluateStorageState();
            emit self->formatVolumeFinished(plan.volume, success);
        }, Qt::QueuedConnection);
    });

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

bool StorageManager::flushMediaPath(const QString& absolutePath, QString* outError) const {
    if (absolutePath.isEmpty()) {
        if (outError) *outError = "Path is empty";
        return false;
    }

    const QString mountPoint = mountedTargetForPath(absolutePath);
    if (mountPoint.isEmpty()) {
        if (outError) {
            *outError = QString("No mounted target found for %1").arg(absolutePath);
        }
        return false;
    }

    return flushMountedPath(mountPoint, outError);
}

bool StorageManager::safeEjectVolume(StorageVolume volume, QString* outError) {
    if (volume == StorageVolume::Nand) {
        if (outError) *outError = "NAND safe eject is not supported";
        return false;
    }

    if (m_recordingQuotaActive) {
        if (outError) *outError = "Safe eject is blocked while recording";
        return false;
    }

    evaluateStorageState();

    const StorageVolumeStatus status = statusForVolume(volume);
    if (!status.ready) {
        if (outError) *outError = QString("%1 is not mounted").arg(volumeName(volume));
        return false;
    }

    if (!flushMountedPath(status.mountPoint, outError)) {
        return false;
    }

    QString commandError;
    if (!runProcessCommand("/usr/bin/umount", {status.mountPoint}, &commandError, 30000)) {
        if (outError) *outError = QString("umount failed: %1").arg(commandError);
        evaluateStorageState();
        return false;
    }

    evaluateStorageState();
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

    const StorageVolumeStatus nand = buildNandStatus();
    if (nand.ready) {
        const QString nandPath = nand.mountPoint + "/" + kDcimSubdir;
        if (QDir(nandPath).exists() && !directories.contains(nandPath)) {
            directories.append(nandPath);
        }
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
    if (m_formatVolume == StorageVolume::SdCard) m_sdCardStatus = {};
    if (m_formatVolume == StorageVolume::UsbDisk) m_usbDiskStatus = {};

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

bool StorageManager::flushMountedPath(const QString& mountPoint, QString* outError) const {
    if (mountPoint.isEmpty()) {
        if (outError) *outError = "Mount point is empty";
        return false;
    }

    if (!isMounted(mountPoint)) {
        if (outError) *outError = QString("%1 is not mounted").arg(mountPoint);
        return false;
    }

    const QByteArray mountPointBytes = mountPoint.toLocal8Bit();
    const int fd = ::open(mountPointBytes.constData(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        if (outError) {
            *outError = QString("open(%1) failed: %2")
                            .arg(mountPoint, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    const int syncResult = ::syncfs(fd);
    const int syncErrno = errno;
    ::close(fd);

    if (syncResult != 0) {
        if (outError) {
            *outError = QString("syncfs(%1) failed: %2")
                            .arg(mountPoint, QString::fromLocal8Bit(std::strerror(syncErrno)));
        }
        return false;
    }

    return true;
}

QString StorageManager::mountedTargetForPath(const QString& absolutePath) const {
    if (absolutePath.isEmpty()) return QString();

    const QString normalizedPath = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath());

    QFile file("/proc/self/mounts");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString bestTarget;
    while (true) {
        const QByteArray rawLine = file.readLine();
        if (rawLine.isNull()) {
            break;
        }

        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        const QStringList fields = line.split(' ', Qt::SkipEmptyParts);
        if (fields.size() < 2) continue;

        const QString target = fields.at(1);
        const bool pathMatchesTarget =
            (normalizedPath == target) || normalizedPath.startsWith(target + "/");
        if (!pathMatchesTarget) continue;

        if (target.size() > bestTarget.size()) {
            bestTarget = target;
        }
    }

    return bestTarget;
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

    struct statvfs stat;
    if (statvfs(mountPoint.toLocal8Bit().constData(), &stat) != 0) {
        return status;
    }

    const quint64 blockSize = static_cast<quint64>(stat.f_frsize);
    const quint64 totalBlocks = static_cast<quint64>(stat.f_blocks);
    const quint64 freeBlocks = static_cast<quint64>(stat.f_bfree);

    // f_bfree includes filesystem-reserved blocks; f_bavail does not.
    status.ready = true;
    status.totalMB = (totalBlocks * blockSize) / (1024 * 1024);
    status.usedMB = ((totalBlocks >= freeBlocks ? totalBlocks - freeBlocks : 0) * blockSize) /
                    (1024 * 1024);
    status.availableMB = (static_cast<quint64>(stat.f_bavail) * blockSize) / (1024 * 1024);
    return status;
}

StorageVolumeStatus StorageManager::buildNandStatus() const {
    return buildMountedVolumeStatus(kNandMountPoint);
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
        return kNandMountPoint;
    }
    return QString();
}
