#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <QObject>
#include <QFuture>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include "core/types.h"

class QSocketNotifier;

enum class StorageVolume : quint8 {
    SdCard = 0,
    UsbDisk,
    Nand
};

enum class RemovableStoragePriority : quint8 {
    SdFirst = 0,
    UsbFirst
};

struct StorageVolumeStatus {
    bool ready = false;  // True when the filesystem is mounted and discoverable.
    QString mountPoint;
    quint64 availableMB = 0;
    quint64 usedMB = 0;
    quint64 totalMB = 0;
};

struct StorageRoutingPolicy {
    RemovableStoragePriority photoPriority = RemovableStoragePriority::SdFirst;
    RemovableStoragePriority videoPriority = RemovableStoragePriority::SdFirst;
    bool allowNandPhotoFallback = true;
};

/**
 * @brief Central authority for file system routing and safety quotas.
 *
 * Owns removable-media detection (SD/USB), internal UBI maintenance, capacity
 * telemetry, and capture routing policy.
 * Upper layers request one file path and never decide media ordering.
 *
 * Features active "Quota Patrol" to prevent system partitions from filling up,
 * potentially causing boot loops or crashes.
 */
class StorageManager : public QObject {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    static StorageManager& instance();

    /**
     * @brief Initializes the Netlink socket for kernel UEVENT listening.
     * @return true if the socket bind was successful.
     */
    bool init();

    /* --- Runtime Status API --- */
    /**
     * @brief Checks if the external SD card is mounted and discoverable.
     */
    bool isSdCardReady() const;

    /**
     * @brief Checks if an external USB disk is mounted and discoverable.
     */
    bool isUsbDiskReady() const;

    /** @brief Returns whether the kernel attached MTD3 as a UBI device. */
    bool isUserdataUbiAttached() const;

    /**
     * @brief Formats one removable volume and remounts it at the canonical mount point.
     *
     * Filesystem policy:
     * - <= 32 GiB  -> FAT32
     * - >  32 GiB  -> exFAT
     */
    bool startFormatVolume(StorageVolume volume);

    /** @brief Creates the UBI userdata volume on a freshly cloned NAND. */
    bool startInitializeUserdata();

    /**
     * @brief Returns the filesystem that @p volume will use after formatting.
     * @return "FAT32", "exFAT", or an empty string when capacity is unavailable.
     */
    QString formatFileSystemName(StorageVolume volume);

    /**
     * @brief Read-only status snapshot for one physical volume.
     */
    StorageVolumeStatus volumeStatus(StorageVolume volume) const;

    /**
     * @brief Current write-routing policy used by requestMediaFilePath().
     */
    StorageRoutingPolicy routingPolicy() const;

    /**
     * @brief Updates runtime routing order for SD/USB without touching UI code.
     * @return true if accepted; false when policy is semantically invalid.
     */
    bool setRoutingPolicy(const StorageRoutingPolicy& policy, QString* outError = nullptr);

    /**
     * @brief Forces kernel write-back for the mounted filesystem owning @p absolutePath.
     * @return true when syncfs succeeded on the owning mountpoint.
     */
    bool flushMediaPath(const QString& absolutePath, QString* outError = nullptr) const;

    /**
     * @brief Flushes and unmounts one removable volume for physical unplug.
     */
    bool safeEjectVolume(StorageVolume volume, QString* outError = nullptr);

    /* --- Capture / Gallery API --- */
    /**
     * @brief Requests a writable path for a new media session.
     *
     * This method acts as a security gateway. It checks:
     * 1. Physical presence of storage.
     * 2. Available disk space against safety thresholds.
     * 3. Permission policy (e.g., No Video on NAND).
     *
     * @param mode The type of media being captured (Photo vs Video).
     * @return Absolute file path (e.g., "/mnt/sdcard/DCIM/VID_...avi") or empty string if denied.
     */
    QString requestMediaFilePath(CaptureMode mode);

    /**
     * @brief Retrieves a list of active and accessible media directories.
     * Evaluates mounted filesystems (SD Card, USB, NAND) dynamically.
     */
    QStringList getAvailableMediaDirectories();

public slots:
    /* --- Runtime Session Hooks --- */
    /**
     * @brief Engages or disengages the active capacity monitoring.
     * Should be called by CaptureService when recording starts/stops.
     *
     * When active, the system polls `statvfs` at 1Hz.
     */
    void setRecordingActive(bool active);

signals:
    /* --- Reactive Notifications --- */
    /**
     * @brief Fired when the SD card is physically inserted or removed.
     * Used by StatusBar to toggle the SD icon.
     */
    void sdCardStateChanged(bool ready);

    /**
     * @brief Fired when the USB disk is physically inserted or removed.
     * Used by StatusBar to toggle the USB icon.
     */
    void usbDiskStateChanged(bool ready);

    /**
     * @brief EMERGENCY SIGNAL: Storage has hit the critical red line.
     * CaptureService must immediately stop recording and close the file.
     */
    void storageSpaceCritical();

    /** @brief Reports completion of one asynchronously formatted removable volume. */
    void formatVolumeFinished(StorageVolume volume, bool success);

    /** @brief Reports real erase progress emitted by ubiformat. */
    void userdataInitializationProgress(int percent);

    /** @brief Reports completion of the UBI userdata initialization sequence. */
    void userdataInitializationFinished(bool success);

private slots:
    /**
     * @brief Handles kernel block-device UEVENTs from Netlink.
     */
    void processNetlinkEvent();

    /**
     * @brief Rebuilds SD/USB readiness snapshot from current mount table.
     */
    void evaluateStorageState();

    /**
     * @brief Enforces recording-time minimum-space safety threshold.
     */
    void enforceActiveQuota();

private:
    /* --- Construction / Ownership --- */
    explicit StorageManager(QObject* parent = nullptr);
    ~StorageManager();

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    /* --- Mount / Device Helpers --- */
    /**
     * @brief Returns true when @p targetPath exists as a mounted destination in /proc/mounts.
     */
    bool isMounted(const QString& targetPath) const;

    /**
     * @brief Resolves mounted source device node for @p targetPath from /proc/self/mounts.
     * @return e.g. "/dev/mmcblk0p1" or empty string if not found.
     */
    QString mountedDeviceForPath(const QString& targetPath) const;

    bool flushMountedPath(const QString& mountPoint, QString* outError) const;
    QString mountedTargetForPath(const QString& absolutePath) const;

    /* --- Filesystem / Capacity Helpers --- */
    quint64 getAvailableSpaceMB(const QString& path) const;
    quint64 getTotalSpaceMB(const QString& path) const;
    bool ensureDirectoryExists(const QString& path) const;
    QString generateTimestampFilename(CaptureMode mode) const;
    StorageVolumeStatus buildMountedVolumeStatus(const QString& mountPoint) const;
    StorageVolumeStatus buildNandStatus() const;

    /* --- Routing / Policy Helpers --- */
    QVector<StorageVolume> removableOrderForMode(CaptureMode mode) const;
    StorageVolumeStatus statusForVolume(StorageVolume volume) const;
    bool isVolumeWritableForMode(StorageVolume volume, CaptureMode mode, quint64* outAvailableMB = nullptr) const;
    QString basePathForVolume(StorageVolume volume) const;

    /* --- Runtime Resources --- */
    int m_netlinkFd = -1;
    QSocketNotifier* m_netlinkNotifier = nullptr;
    QTimer* m_quotaTimer = nullptr;

    StorageVolumeStatus m_sdCardStatus;
    StorageVolumeStatus m_usbDiskStatus;
    StorageRoutingPolicy m_routingPolicy;
    bool m_recordingQuotaActive = false;
    bool m_hasActiveRecordingVolume = false;
    StorageVolume m_activeRecordingVolume = StorageVolume::SdCard;
    std::optional<StorageVolume> m_formatVolume;
    QFuture<void> m_maintenanceFuture;

    /* --- Canonical Mount Constants --- */
    static const QString kSdCardMountPoint;
    static const QString kUsbDiskMountPoint;
    static const QString kNandMountPoint;
    static const QString kDcimSubdir;

    /* --- Space / Timing Thresholds --- */
    static constexpr quint64 kMinRecordSpaceMB = 50; /**< Refuse new video if below this */
    static constexpr quint64 kMinPhotoSpaceMB  = 1;  /**< P99-derived minimum free space */
    static constexpr quint64 kCriticalSpaceMB  = 2;  /**< Stop active recording at or below this */
    static constexpr int kNetlinkSettleDelayMs = 750;
};

#endif // STORAGE_MANAGER_H
