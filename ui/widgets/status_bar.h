#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <QWidget>
#include "core/types.h"

/**
 * @brief Top-level status bar that visualizes system states.
 * Uses a push-based architecture via EventBus to minimize hardware polling.
 */
class StatusBar : public QWidget {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);
    virtual ~StatusBar() = default;

    /** @brief Updates the visual transparency of the bar's content. */
    void setContentsOpacity(qreal opacity) { m_contentsOpacity = opacity; update(); }

public slots:
    /** @brief Triggered when battery hardware state changes. */
    void onPowerStatusChanged(const BatteryStatus& status);

    /** @brief Triggered when emissivity is adjusted in settings. */
    void onEmissivityChanged(float value);

    /** @brief Internal 1Hz pulse for clock updates. */
    void onSecondTick();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // --- Modular Drawing Actors ---
    void drawTime(QPainter& p, const QRect& rect);
    void drawEmissivity(QPainter& p, const QRect& rect);
    void drawBattery(QPainter& p, const QRect& rect);

    /** @brief Helper for high-contrast outlined text. */
    void drawOutlinedText(QPainter& p, const QRect& rect, int flags, const QString& text);

    // --- Cached State ---
    QString m_timeText;
    float m_emissivity = 0.00f;
    BatteryStatus m_batteryStatus;
    qreal m_contentsOpacity = 1.0;

    // --- UI Constants ---
    static constexpr int LOW_BATTERY_THRESHOLD = 20;

    // --- Visual Configuration Ratios ---
    const qreal kHorizontalMarginRatio = 0.25;  // Margin from screen edges, relative to bar height
    const qreal kItemSpacingRatio    = 0.50;  // Spacing between items, relative to bar height
    const qreal kTextSizeRatio   = 0.65; // Font size relative to bar height

    // --- Semantic Color Palette ---
    const QColor BATT_SURFACE  = QColor("#505050");
    const QColor BATT_FILL_CHG = QColor("#34C759");
    const QColor BATT_FILL_STD = QColor("#FFFFFF");
    const QColor BATT_FILL_LOW = QColor("#FF3B30");
    const QColor BATT_TEXT_CHG = QColor("#FFFFFF");
    const QColor BATT_TEXT_STD = QColor("#000000");
    const QColor BATT_MARK_ERR = QColor("#FF3B30");
};

#endif // STATUS_BAR_H
