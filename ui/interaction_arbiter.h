#ifndef INTERACTION_ARBITER_H
#define INTERACTION_ARBITER_H

#include <QObject>
#include <QPoint>
#include <QList>
#include <QEvent>

class App;
class QTimer;
class QWidget;
class GestureRecognizer;
struct RawTouchPoint;

/**
 * @brief The Logic Brain & Event Arbiter.
 *
 * Responsibilities:
 * 1. Receives screen-mapped events from EventBus (InputManager handles rotation).
 * 2. Uses GestureRecognizer to translate raw data into semantic gestures.
 * 3. Arbitrates between Global Gestures (Pull-down) and Local View Gestures.
 * 4. Injects atomic events (Hover, Click, LongPress) into specific Widgets.
 * 5. Manages System Power (3s shutdown hold).
 */
class InteractionArbiter : public QObject {
    Q_OBJECT
public:
    static InteractionArbiter& instance();

    /**
     * @brief Binds the controller to the main application container.
     */
    void init(App* app);

private slots:
    /* --- Raw Hardware Inputs (from EventBus) --- */
    void handleRawKey(bool pressed);
    void handleRawTouch(const QList<RawTouchPoint>& points);

    /* --- Semantic Gesture Handlers (from GestureRecognizer) --- */
    void onTouchesStarted();
    void onTouchesReleased(const QPoint& start, int dx, int dy, float vx, float vy);
    void onTapDetected(const QPoint& start, int dx, int dy);
    void onDoubleTapDetected(const QPoint& start, int dx, int dy);
    void onLongPressDetected(const QPoint& start);

    void onSwipeUpdate(const QPoint& start, int dx, int dy);
    void onPinchUpdate(const QPoint& center, float factor);

    /* --- System Logic --- */
    void onKeyLongPressTimeout();

private:
    explicit InteractionArbiter(QObject *parent = nullptr);

    App* m_app = nullptr;
    GestureRecognizer* m_recognizer = nullptr;
    QTimer* m_shutdownTimer = nullptr;

    /* Interaction State */
    bool m_isGlobalGesture = false;    /**< True if controlling System Overlay (Layer 2) */
    bool m_intentLocked = false;       /**< True if gesture type (Global vs View) is decided */
    bool m_wasTouching = false;        /**< Tracking rising edge of touch */

    /* Hover Tracking */
    QWidget* m_pressedWidget = nullptr; /**< Current active physical or button target */
    QWidget* m_currentHoveredWidget = nullptr;

    /* Constants & Config */
    static constexpr int SHUTDOWN_HOLD_MS = 3000;
    static constexpr float TOP_EDGE_RATIO = 0.12f; /**< Top 12% triggers global menu */
    static constexpr int HIT_EXPANSION_PX = 10;    /**< Expanded hit area for small buttons */
    static const char* PROP_ALLOW_SLIDE_TRIGGER;
    static const char* PROP_IS_INTERACTABLE;

    /* Internal Helpers */
    QWidget* findTargetWidget(const QPoint& globalPos);
    QWidget* findTopModalWidget();
    void injectMouseEvent(QWidget* target, QEvent::Type type, const QPoint& globalPos);
    void updateHoverState(const QPoint& globalPos);
    void clearHoverState();
    bool isViewInteractionAllowed(const QPoint& globalPos);

};

#endif // INTERACTION_ARBITER_H
