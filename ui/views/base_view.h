#ifndef BASE_VIEW_H
#define BASE_VIEW_H

#include <QWidget>
#include <QPoint>

/**
 * @brief The abstract contract for all full-screen views.
 *
 * Defines a complete set of semantic gesture handlers that align with the
 * GestureRecognizer's output signals. This allows UIController to dispatch
 * high-level interaction commands without knowing the view's implementation details.
 *
 * NOTE: Unused parameter names in the base virtual function definitions are
 * intentionally commented out to suppress compiler warnings, while preserving
 * the full signature for derived classes to implement.
 */
class BaseView : public QWidget {
    Q_OBJECT
public:
    explicit BaseView(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~BaseView() = default;

    /* --- Lifecycle Hooks (called by App) --- */
    virtual void onEnter() {}
    virtual void onExit() {}

    /* --- Widget Discovery (For UIController Hit-Testing) --- */

    // Allows UIController to know if the user tapped the capsule or mode button
    virtual QWidget* capsuleWidget() { return nullptr; }
    virtual QWidget* modeSelectorWidget() { return nullptr; }

    /* --- Hardware Key Dispatcher (from UIController) --- */
    /**
     * @brief Must be implemented by derived classes to handle the primary physical button.
     */
    virtual void handleKeyShortPress() = 0;

    /* --- Gesture Dispatchers (from UIController) --- */

    /** @brief Interrupts all ongoing animations the moment a touch is detected. */
    virtual void onGestureStarted() {}

    /** @brief Handles single-tap gestures. */
    virtual void onTapDetected(const QPoint& /*pos*/) {}

    /** @brief Handles single-finger press-and-hold gestures. */
    virtual void onLongPressDetected(const QPoint& /*pos*/) {}

    /** @brief Handles double-tap gestures, e.g., for zooming in the gallery. */
    virtual void onDoubleTapDetected(const QPoint& /*pos*/) {}

    /**
     * @brief Tracks continuous single-finger movement for UI elements to follow.
     * @param start The absolute screen coordinate where the gesture began.
     * @param dx Accumulated horizontal displacement from 'start'.
     * @param dy Accumulated vertical displacement from 'start'.
     */
    virtual void onGestureUpdate(const QPoint& /*start*/, int /*dx*/, int /*dy*/) {}

    /**
     * @brief Finalizes a swipe, providing velocity for inertia-based animations.
     * @param vx Horizontal velocity in pixels per millisecond.
     * @param vy Vertical velocity in pixels per millisecond.
     */
    virtual void onGestureFinished(const QPoint& /*start*/, int /*dx*/, int /*dy*/, float /*vx*/, float /*vy*/) {}

    /**
     * @brief Handles multi-touch pinch gestures for scaling.
     * @param center The geometric midpoint between the two fingers.
     * @param factor The relative scale change since the last update.
     */
    virtual void onPinchUpdate(const QPoint& /*center*/, float /*factor*/) {}

    /**
     * @brief Retrieve all temporary state controls on the current interface.
     */
    virtual void resetTransientUi() {}

};

#endif // BASE_VIEW_H
