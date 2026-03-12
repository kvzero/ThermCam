#ifndef TYPES_H
#define TYPES_H

#include <QMetaType>
#include <QImage>
#include <QByteArray>
#include <QList>
#include <QPoint>

/**
 * @brief Global notification priority levels.
 */
enum class ToastLevel {
    Info,      /**< Standard feedback  */
    Warning,   /**< Attention required  */
    Critical   /**< Failure / Danger */
};

/**
 * @brief Raw temperature point data (Hardware independent).
 */
struct TempPt {
    int x = 0;
    int y = 0;
    float temperature = 0.0f;

    /* Explicit constructors required for C++11 non-aggregate types */
    TempPt() = default;
    TempPt(int _x, int _y, float _t) : x(_x), y(_y), temperature(_t) {}
};

/**
 * @brief Raw frame data directly from the camera driver.
 */
struct RawFrame {
    QByteArray pixelData;
    int w = 0;
    int h = 0;
    TempPt hot_spot;
    TempPt cold_spot;
    TempPt center_spot;
};
Q_DECLARE_METATYPE(RawFrame)

/**
 * @brief Processed frame ready for UI rendering.
 */
struct VisualFrame {
    QImage image;           // RGA scaled image
    TempPt hot_spot;        // Mapped coordinates
    TempPt cold_spot;       // Mapped coordinates
    TempPt center_spot;     // Mapped coordinates
};
Q_DECLARE_METATYPE(VisualFrame)

/**
 * @brief Represents a raw touch point from the Linux Input device.
 */
struct RawTouchPoint {
    int id = -1;       /**< Slot ID (Tracking ID) */
    int x = 0;         /**< Raw X coordinate from digitizer */
    int y = 0;         /**< Raw Y coordinate from digitizer */
    bool active = false; /**< True if the finger is currently on the screen */

    RawTouchPoint() = default;
    RawTouchPoint(int _id, int _x, int _y, bool _active)
        : id(_id), x(_x), y(_y), active(_active) {}
};
Q_DECLARE_METATYPE(RawTouchPoint);

/**
 * @brief Lightweight battery status used for high-frequency UI updates.
 * Designed for global broadcasting via EventBus (e.g., for StatusBar).
 */
struct BatteryStatus {
    int level = 0;                   ///< Battery capacity percentage (0-100).
    bool isPresent = false;          ///< Check if the battery is present.
    bool isChargerConnected = false; ///< True if VBUS/External power is physically detected.
    bool isCharging = false;         ///< True if the charging logic is active (Current > 0 into battery).

    /**
     * @brief Equality operator for state deduplication in HAL.
     */
    bool operator==(const BatteryStatus& other) const {
        return level == other.level &&
               isPresent == other.isPresent &&
               isChargerConnected == other.isChargerConnected &&
               isCharging == other.isCharging;
    }
    bool operator!=(const BatteryStatus& other) const { return !(*this == other); }
};
Q_DECLARE_METATYPE(BatteryStatus)

/**
 * @brief Full telemetry snapshot for detailed power analysis.
 * Retrieved via polling (Pull mode) typically by SettingsView.
 */
struct BatteryInfo {
    BatteryStatus status;           ///< Nested core status for convenience.

    int voltage_mV = 0;             ///< Battery terminal voltage in millivolts.
    int current_mA = 0;             ///< Real-time current in milliamps (Positive: Charging, Negative: Discharging).
    int full_capacity_mAh = 0;      ///< Full Charge Capacity (FCC), indicates battery health.
    int design_capacity_mAh = 0;    ///< Design Capacity of the battery pack.
    int cycle_count = -1;           ///< Number of charge cycles (-1 if not supported by driver).
    QString health;                 ///< Health string from driver (e.g., "Good", "Overheat").
};

/**
 * @brief Operation modes for the media capture system.
 */
enum class CaptureMode {
    Photo,      /**< Single image acquisition */
    Video       /**< Continuous frame sequence recording */
};
Q_DECLARE_METATYPE(CaptureMode)

/**
 * @brief Finite states for the video recording engine.
 */
enum class RecordingState {
    Idle,       /**< Ready to record, no file open */
    Recording,  /**< Active encoder session, writing to disk */
    Paused      /**< Encoder session active, frame writing suspended */
};

/**
 * @brief Lightweight representation of a media file on disk.
 * Strictly prohibits holding QImage data to prevent memory exhaustion.
 */
struct MediaFileInfo {
    QString filePath;     ///< Absolute path serving as the unique ID
    CaptureMode type;     ///< Photo or Video
    qint64 timestamp;     ///< Epoch time for chronological sorting
    QString durationStr;  ///< Pre-formatted duration (e.g., "00:15"), empty for photos

    bool operator==(const MediaFileInfo& other) const {
        return filePath == other.filePath;
    }
};
Q_DECLARE_METATYPE(MediaFileInfo)

#endif // TYPES_H
