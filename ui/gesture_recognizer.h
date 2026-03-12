#ifndef GESTURE_RECOGNIZER_H
#define GESTURE_RECOGNIZER_H

#include <QObject>
#include <QPoint>
#include <QElapsedTimer>
#include <QList>
#include <QTimer>
#include "core/types.h"

/**
 * @brief Professional Gesture Recognition Engine.
 *
 * Implements a Finite State Machine (FSM) to translate raw touch points
 * into semantic gestures (Swipe, Tap, LongPress, Pinch).
 *
 * COMPATIBILITY NOTE:
 * Uses explicit constructors for the Config struct to ensure strict C++11
 * compliance on older embedded compilers (fixing the "default member initializer" error).
 */
class GestureRecognizer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Configuration parameters for gesture sensitivity.
     */
    struct Config {
        int swipeThreshold;      /**< Pixels to confirm a swipe */
        int tapMaxTimeMs;        /**< Max duration for a valid tap */
        int doubleTapIntervalMs; /**< Max time between two taps */
        int longPressTimeMs;     /**< Threshold for long press */
        int deadzone;            /**< Jitter suppression */
        float velocityWeight;    /**< IIR filter weight for velocity (0.0-1.0) */

        /**
         * @brief Explicit constructor to define defaults.
         * Solves C++11 parsing issues with nested structs in some toolchains.
         */
        Config()
            : swipeThreshold(10)
            , tapMaxTimeMs(250)
            , doubleTapIntervalMs(300)
            , longPressTimeMs(1500)
            , deadzone(10)
            , velocityWeight(0.3f) {}
    };

    explicit GestureRecognizer(const Config& cfg = Config(), QObject* parent = nullptr);

    /**
     * @brief Process a frame of touch points from InputManager.
     */
    void update(const QList<RawTouchPoint>& points);

    /**
     * @brief Force reset the internal state machine to Idle.
     */
    void reset();

signals:
    /** @brief Emitted instantly on first touch to interrupt animations. */
    void touchesStarted();

    /**
     * @brief Fired when all fingers are lifted.
     * @param vx Horizontal velocity in px/ms.
     * @param vy Vertical velocity in px/ms.
     */
    void touchesReleased(const QPoint& start, int dx, int dy, float vx, float vy);

    /** @brief Single Tap detected (on release). */
    void tapDetected(const QPoint& start, int dx, int dy);

    /** @brief Double Tap detected. */
    void doubleTapDetected(const QPoint& start, int dx, int dy);

    /** @brief Long Press detected (finger held static). */
    void longPressDetected(const QPoint& start);

    /**
     * @brief Continuous movement update.
     * @param start The point where the gesture began.
     * @param dx Accumulated X displacement (current - start).
     * @param dy Accumulated Y displacement (current - start).
     */
    void swipeUpdate(const QPoint& start, int dx, int dy);

    /** @brief Multi-finger pinch update. */
    void pinchUpdate(const QPoint& center, float factor);

private:
    enum State {
        State_Idle,
        State_Pending,      /**< Waiting to determine gesture type */
        State_Swiping,      /**< Confirmed movement */
        State_LongPressed,  /**< Long press triggered (can still move) */
        State_Pinching      /**< Two fingers detected */
    };

    void handleSingleTouch(const RawTouchPoint& p);
    void handleMultiTouch(const QList<RawTouchPoint>& points);
    float calculateDistance(const QPoint& p1, const QPoint& p2);

    Config m_cfg;
    State m_state; // Initialized in constructor
    QTimer* m_longPressTimer;

    /* Tracking Data */
    QPoint m_startPos;
    QPoint m_lastPos;
    QElapsedTimer m_timer;

    /* Velocity Tracking */
    float m_vx;
    float m_vy;

    /* Double Tap Tracking */
    QPoint m_lastTapPos;
    QElapsedTimer m_doubleTapTimer;
};

#endif // GESTURE_RECOGNIZER_H
