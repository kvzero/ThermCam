#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <QWidget>
#include <QList>
#include "core/types.h"

class QSocketNotifier;

/**
 * @brief Top-level status bar that visualizes system states.
 * Uses a push-based architecture via EventBus to minimize hardware polling.
 */
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);
    ~StatusBar() override;

    /** @brief Updates the visual transparency of the bar's content. */
    void setContentsOpacity(qreal opacity) { m_contentsOpacity = opacity; update(); }

public slots:
    /** @brief Triggered when battery hardware state changes. */
    void onPowerStatusChanged(const BatteryStatus& status);

    /** @brief Triggered when emissivity is adjusted in settings. */
    void onEmissivityChanged(float value);

    /** @brief Internal 1Hz pulse for clock updates. */
    void onSecondTick();

    /** @brief Triggered when SD card insert/remove state changes. */
    void onSdCardStateChanged(bool ready);

    /** @brief Triggered when USB disk insert/remove state changes. */
    void onUsbDiskStateChanged(bool ready);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void initUdcMonitoring();
    void refreshPcConnection();

    // --- Modular Drawing Actors ---
    /** @return Left edge of the right cluster's painted bounds. */
    qreal drawRightStatusItems(QPainter& p, const QRect& barRect, qreal rightEdge);
    void drawTime(QPainter& p, const QRect& rect);
    void drawEmissivity(QPainter& p, const QRect& rect);

    /** @brief Draws the self-sized battery with its painted right edge aligned. */
    void drawBattery(QPainter& p, qreal visualRightX, const QRect& barRect);

    /** @brief Draws a status glyph with its ink right edge aligned. */
    void drawStatusIcon(QPainter& p, QChar icon, qreal visualRightX, const QRect& barRect);

    /** @brief Helper for high-contrast outlined text. */
    void drawOutlinedText(QPainter& p, const QRect& rect, int flags, const QString& text,
                          const QColor& textColor = Qt::white);

    // --- Cached State ---
    QString m_timeText;
    float m_emissivity = 0.00f;
    BatteryStatus m_batteryStatus;
    bool m_sdCardReady = false;
    bool m_usbDiskReady = false;
    bool m_pcConnected = false;
    qreal m_contentsOpacity = 1.0;

    // UDC state files send POLLPRI through sysfs_notify on USB state changes.
    QList<int> m_udcStateFds;
    QList<QSocketNotifier*> m_udcStateNotifiers;

    // --- UI Constants ---
    static constexpr int LOW_BATTERY_THRESHOLD = 20;

    // --- Visual Configuration Ratios ---
    const qreal kHorizontalInsetWidthRatio = 0.03;    // Left/right content inset, relative to bar width
    const qreal kLeftClusterGapWidthRatio  = 0.045;    // Gap between Time and Emissivity, relative to bar width
    const qreal kRightVisualGapWidthRatio  = 0.031;    // Gap between adjacent right-side visual bounds
    const qreal kContentYOffsetRatio       = 0.08;     // Global downward shift of all status bar content
    const qreal kTextSizeRatio             = 0.6;      // Font size relative to bar height

    // --- Semantic Color Palette ---
    const QColor BATT_SURFACE  = QColor("#7A8088");
    const QColor BATT_FILL_CHG = QColor("#34C759");
    const QColor BATT_FILL_STD = QColor("#FFFFFF");
    const QColor BATT_FILL_LOW = QColor("#FF3B30");
    const QColor BATT_TEXT_CHG = QColor("#FFFFFF");
    const QColor BATT_TEXT_STD = QColor("#2B2B2B");
    const QColor BATT_MARK_ERR = QColor("#FF3B30");
    const QColor EMISSIVITY_TEXT_COLOR = QColor("#FFC84A");

    static constexpr ushort ICON_SD_CARD = 0xf384;
    static constexpr ushort ICON_USB_DISK = 0xfc59;
    static constexpr ushort ICON_PC_CONNECTION = 0xf00c;
};

#endif // STATUS_BAR_H
