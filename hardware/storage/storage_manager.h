#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVector>
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
 * Owns removable-media detection (SD/USB), capacity telemetry, and capture routing policy.
 * Upper layers request one file path and never decide media ordering.
 *
 * Features active "Quota Patrol" to prevent system partitions from filling up,
 * potentially causing boot loops or crashes.
 */
class StorageManager : public QObject {
    Q_OBJECT
public:
    static StorageManager& instance();

    /**
     * @brief Initializes the Netlink socket for kernel UEVENT listening.
     * @return true if the socket bind was successful.
     */
    bool init();

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

    /**
     * @brief Checks if the external SD card is mounted and discoverable.
     */
    bool isSdCardReady() const;

    /**
     * @brief Checks if an external USB disk is mounted and discoverable.
     */
    bool isUsbDiskReady() const;

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

public slots:
    /**
     * @brief Engages or disengages the active capacity monitoring.
     * Should be called by CaptureService when recording starts/stops.
     *
     * When active, the system polls `statvfs` at 1Hz.
     */
    void setRecordingActive(bool active);

signals:
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

private slots:
    void processNetlinkEvent();
    void evaluateStorageState();
    void enforceActiveQuota();

private:
    explicit StorageManager(QObject* parent = nullptr);
    ~StorageManager();

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    /* Internal Helpers */
    bool isMounted(const QString& targetPath) const;
    quint64 getAvailableSpaceMB(const QString& path) const;
    quint64 getTotalSpaceMB(const QString& path) const;
    bool ensureDirectoryExists(const QString& path) const;
    QString generateTimestampFilename(CaptureMode mode) const;
    StorageVolumeStatus buildMountedVolumeStatus(const QString& mountPoint) const;
    StorageVolumeStatus buildNandStatus() const;
    QString resolveUsbMountPoint() const;
    QVector<StorageVolume> removableOrderForMode(CaptureMode mode) const;
    StorageVolumeStatus statusForVolume(StorageVolume volume) const;
    bool isVolumeWritableForMode(StorageVolume volume, CaptureMode mode, quint64* outAvailableMB = nullptr) const;
    QString basePathForVolume(StorageVolume volume) const;

    /* Hardware Resources */
    int m_netlinkFd = -1;
    QSocketNotifier* m_netlinkNotifier = nullptr;
    QTimer* m_quotaTimer = nullptr;

    StorageVolumeStatus m_sdCardStatus;
    StorageVolumeStatus m_usbDiskStatus;
    StorageRoutingPolicy m_routingPolicy;
    bool m_recordingQuotaActive = false;
    bool m_hasActiveRecordingVolume = false;
    StorageVolume m_activeRecordingVolume = StorageVolume::SdCard;

    /* Configuration Constants */
    static const QString kSdCardMountPoint;
    static const QString kNandFallbackBase;
    static const QString kDcimSubdir;
    static const QStringList kUsbMountPointCandidates;

    static constexpr quint64 kMinRecordSpaceMB = 200; /**< Refuse new video if below this */
    static constexpr quint64 kMinPhotoSpaceMB  = 50;  /**< Refuse new photo if below this */
    static constexpr quint64 kCriticalSpaceMB  = 100; /**< Force stop recording if below this */
    static constexpr int kNetlinkSettleDelayMs = 750;
};

#endif // STORAGE_MANAGER_H
