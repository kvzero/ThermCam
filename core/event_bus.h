#ifndef CORE_EVENT_BUS_H
#define CORE_EVENT_BUS_H

#include <QObject>
#include <QRect>
#include <QString>
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
     * @brief Fired immediately when the physical primary key is pressed.
     */
    void keyPressed();

    /**
     * @brief Fired when the primary key is released inside the short-press window.
     */
    void keyShortPressed();

    /**
     * @brief Fired once when the primary key remains held for the long-press window.
     */
    void keyLongPressed();

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
