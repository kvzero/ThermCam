#ifndef INTERACTION_ARBITER_H
#define INTERACTION_ARBITER_H

#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>

#include "ui/interaction_target.h"

class App;
class BaseView;
class GestureRecognizer;
class QTimer;
class QWidget;
struct RawTouchPoint;

/**
 * @brief Central touch/key router that owns one interaction session at a time.
 *
 * InteractionArbiter lives for the whole app lifecycle. It consumes hardware
 * input from EventBus, feeds GestureRecognizer, and dispatches interaction
 * callbacks to exactly one `InteractionTarget` during each touch session.
 *
 * Global pull-down gesture detection remains in this layer and preempts local
 * target updates when the global route is selected.
 */
class InteractionArbiter : public QObject {
    Q_OBJECT
public:
    static InteractionArbiter& instance();

    /**
     * @brief Binds the controller to the main application container.
     */
    void init(App* app);

    /**
     * @brief Cancels the current touch session and suppresses events until release.
     */
    void cancelTouchSession();

private slots:
    /* --- Raw Hardware Inputs (from EventBus) --- */
    void handleRawKey(bool pressed);
    void handleRawTouch(const QList<RawTouchPoint>& points);

    /* --- Semantic Gesture Handlers (from GestureRecognizer) --- */
    void onRecognizerTouchesStarted();
    void onRecognizerTouchesReleased(const QPoint& start, int dx, int dy, float vx, float vy);
    void onRecognizerTap(const QPoint& start, int dx, int dy);
    void onRecognizerDoubleTap(const QPoint& start, int dx, int dy);
    void onRecognizerLongPress(const QPoint& start);
    void onRecognizerSwipeUpdate(const QPoint& start, int dx, int dy);
    void onRecognizerPinchUpdate(const QPoint& center, float factor);

    /* --- System Logic --- */
    void onKeyLongPressTimeout();

private:
    explicit InteractionArbiter(QObject *parent = nullptr);

    enum class ReleasedSemanticRoute : quint8 {
        None = 0,
        View
    };

    App* m_app = nullptr;
    GestureRecognizer* m_recognizer = nullptr;
    QTimer* m_shutdownTimer = nullptr;

    /* --- Interaction Session State --- */
    bool m_isGlobalGesture = false; /**< True when current swipe is routed as global pull-down. */
    bool m_intentLocked = false;    /**< True once global-vs-local route is decided for this swipe. */
    bool m_wasTouching = false;     /**< Tracks the raw touch rising/falling edge. */
    int m_activePointerCount = 0;

    /* --- Session Owner State --- */
    InteractionTarget* m_ownerTarget = nullptr;
    QWidget* m_ownerWidget = nullptr;
    QPoint m_ownerStartGlobal;
    QPoint m_ownerPrevGlobal;
    ReleasedSemanticRoute m_releasedSemanticRoute = ReleasedSemanticRoute::None;

    /* --- Constants --- */
    static constexpr int SHUTDOWN_HOLD_MS = 3000;
    static constexpr float TOP_EDGE_RATIO = 0.12f; /**< Top 12% can trigger global pull-down. */
    static constexpr int HIT_EXPANSION_PX = 10;    /**< Fuzzy hit radius for small controls. */
    static const char* PROP_ALLOW_SLIDE_TRIGGER;
    static const char* PROP_IS_INTERACTABLE;

    /* --- Internal Helpers --- */
    BaseView* activeViewTarget() const;
    bool ownerIsActiveView() const;
    InteractionTarget* targetFromWidget(QWidget* widget) const;
    QWidget* findTargetWidget(const QPoint& globalPos);
    void probeInitialOwnerIntent(const QPoint& anchorGlobal);
    BaseView* releasedSemanticViewTarget() const;
    void setOwner(InteractionTarget* target, QWidget* widget, const QPoint& anchorGlobal);
    void clearOwner();
    InteractionEvent buildEventFor(QWidget* widget,
                                   const QPoint& startGlobal,
                                   const QPoint& previousGlobal,
                                   const QPoint& currentGlobal,
                                   const QPointF& velocityPxPerMs) const;
    InteractionEvent buildSemanticEvent(const QPoint& globalPos) const;
};

#endif // INTERACTION_ARBITER_H
