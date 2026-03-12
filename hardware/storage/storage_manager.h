#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include "core/types.h"

class QSocketNotifier;

/**
 * @brief Central authority for file system routing and safety quotas.
 *
 * Implements a strict storage hierarchy:
 * 1. SD Card (Preferred): Used for Video and High-Res Photos.
 * 2. NAND Flash (Fallback): Emergency photo storage only. Video is strictly prohibited.
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
     * Evaluates mounted filesystems (SD Card, NAND) dynamically.
     */
    QStringList getAvailableMediaDirectories() const;

    /**
     * @brief Checks if the external SD card is mounted and writable.
     */
    bool isSdCardReady() const;

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
     * @brief EMERGENCY SIGNAL: Storage has hit the critical red line.
     * CaptureService must immediately stop recording and close the file.
     */
    void storageSpaceCritical();

private slots:
    void processNetlinkEvent();
    void evaluateSdCardState();
    void enforceActiveQuota();

private:
    explicit StorageManager(QObject* parent = nullptr);
    ~StorageManager();

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    /* Internal Helpers */
    bool isMounted(const QString& targetPath) const;
    quint64 getAvailableSpaceMB(const QString& path) const;
    bool ensureDirectoryExists(const QString& path) const;
    QString generateTimestampFilename(CaptureMode mode) const;

    /* Hardware Resources */
    int m_netlinkFd = -1;
    QSocketNotifier* m_netlinkNotifier = nullptr;
    QTimer* m_quotaTimer = nullptr;

    bool m_sdCardReady = false;

    /* Configuration Constants */
    static const QString kSdCardMountPoint;
    static const QString kNandFallbackBase;
    static const QString kDcimSubdir;

    static constexpr quint64 kMinRecordSpaceMB = 200; /**< Refuse new video if below this */
    static constexpr quint64 kMinPhotoSpaceMB  = 50;  /**< Refuse new photo if below this */
    static constexpr quint64 kCriticalSpaceMB  = 100; /**< Force stop recording if below this */
};

#endif // STORAGE_MANAGER_H
