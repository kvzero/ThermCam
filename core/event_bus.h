#ifndef CORE_EVENT_BUS_H
#define CORE_EVENT_BUS_H

#include <QObject>
#include "core/types.h"

/**
 * @brief Central event dispatcher for decoupled communication.
 */
class EventBus : public QObject {
    Q_OBJECT
public:
    static EventBus& instance();

signals:
    /**
     * @brief Fired when the physical Event-Key state changes.
     * @param pressed True if the button is physically held down.
     */
    void rawKeySignal(bool pressed);

    /**
     * @brief Fired when a new multi-touch synchronization frame is ready.
     * @param points A snapshot of all active touch slots (Protocol B).
     */
    void rawTouchSignal(const QList<RawTouchPoint>& points);

    /**
     * @brief Requests a system-wide toast notification.
     * @param message The text content to display.
     * @param level The severity level (Info, Warning, Critical).
     */
    void toastRequested(const QString &message, ToastLevel level);

    /**
     * @brief Requests a haptic feedback effect.
     * @param effectId The waveform ID defined in the HAL.
     */
    void hapticRequested(int effectId);

    /**
     * @brief Fired ONLY when the core battery status changes.
     * Use this signal to update passive UI elements like StatusBar.
     * @param status The new lightweight battery state.
     */
    void powerStatusChanged(const BatteryStatus& status);

    /**
     * @brief Fired when the thermal camera's emissivity value is changed.
     * @param value The new emissivity value (e.g., 0.95).
     */
    void emissivityChanged(float value);

    /**
     * @brief Fired when temperature display unit changes.
     * @param isFahrenheit True for Fahrenheit (°F), false for Celsius (°C).
     */
    void temperatureUnitChanged(bool isFahrenheit);

    /**
     * @brief Fired when pseudo-color palette changes in runtime settings.
     * @param paletteId ThermalPalette::Id serialized as integer.
     */
    void paletteChanged(int paletteId);

    /**
     * @brief Global view routing request with optional transition anchor.
     * @param sourceAnchor Optional geometry used as the morph start anchor.
     * @param transitionMode Route-level transition strategy for this request.
     */
    void cameraRequested(const QRect& sourceAnchor,
                         TransitionMode transitionMode);
    void galleryRequested(const QRect& sourceAnchor,
                          TransitionMode transitionMode);
    void settingsRequested(const QRect& sourceAnchor,
                           TransitionMode transitionMode);
    void paletteSelectorRequested();

private:
    explicit EventBus(QObject *parent = nullptr);
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
};

#endif // CORE_EVENT_BUS_H
