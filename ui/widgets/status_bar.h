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

    /** @return Left painted edge, including the outline, for right-to-left layout. */
    qreal drawBattery(QPainter& p, qreal visualRightX, const QRect& barRect);
    qreal drawStatusIcon(QPainter& p, QChar icon, qreal visualRightX, const QRect& barRect);

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

};

#endif // STATUS_BAR_H
