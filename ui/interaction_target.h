#ifndef UI_INTERACTION_TARGET_H
#define UI_INTERACTION_TARGET_H

#include <QPoint>
#include <QPointF>
#include <QtGlobal>

/**
 * @brief Canonical per-session interaction snapshot distributed by InteractionArbiter.
 *
 * The arbiter owns this model and guarantees that all coordinates in one event
 * represent the same physical frame:
 * - Global fields are in App root coordinates.
 * - Local fields are mapped into the current owner target's coordinate space.
 */
struct InteractionEvent {
    /** @brief Global position where the current session started. */
    QPoint startGlobal;
    /** @brief Previous global sample in the same session. */
    QPoint previousGlobal;
    /** @brief Current global sample in the same session. */
    QPoint currentGlobal;
    /** @brief Delta from `startGlobal` to `currentGlobal`. */
    QPoint deltaFromStartGlobal;
    /** @brief Delta from `previousGlobal` to `currentGlobal`. */
    QPoint deltaFromPreviousGlobal;

    /** @brief Local position (owner widget space) at session start. */
    QPoint startLocal;
    /** @brief Previous local sample in owner widget space. */
    QPoint previousLocal;
    /** @brief Current local sample in owner widget space. */
    QPoint currentLocal;
    /** @brief Delta from `startLocal` to `currentLocal`. */
    QPoint deltaFromStartLocal;
    /** @brief Delta from `previousLocal` to `currentLocal`. */
    QPoint deltaFromPreviousLocal;

    /** @brief Pointer velocity in pixels per millisecond (global frame). */
    QPointF velocityPxPerMs;
    /** @brief Active pointer count reported by recognizer for this sample. */
    int pointerCount = 1;
    /** @brief Event timestamp from monotonic app wall time (ms). */
    qint64 timestampMs = 0;
};

/**
 * @brief Ownership decision returned by onInteractionUpdate().
 *
 * KeepOwner means the current target remains the single interaction owner.
 *
 * ReleaseOwner means the target intentionally yields control to arbiter
 * before physical touch release. This path is treated as a route switch and
 * will trigger `onInteractionCancel()` on the current owner (not
 * `onInteractionEnd()`). `onInteractionEnd()` is reserved for physical release
 * only.
 */
enum class InteractionUpdateDecision : quint8 {
    KeepOwner = 0,
    ReleaseOwner
};

/**
 * @brief Unified interaction owner contract for views and interactive widgets.
 *
 * Exactly one InteractionTarget owns a touch session at a time. The arbiter
 * dispatches begin/update/end/cancel to that owner only.
 */
class InteractionTarget {
public:
    virtual ~InteractionTarget() = default;

    /** @brief Starts a new owner session. */
    virtual void onInteractionBegin(const InteractionEvent& event) = 0;

    /**
     * @brief Updates the active owner session.
     * @return KeepOwner to continue ownership, ReleaseOwner to yield control.
     */
    virtual InteractionUpdateDecision onInteractionUpdate(const InteractionEvent& event) = 0;

    /** @brief Completes the owner session on physical release. */
    virtual void onInteractionEnd(const InteractionEvent& event) = 0;

    /** @brief Cancels the owner session due to route switch or forced takeover. */
    virtual void onInteractionCancel() = 0;

    /** @brief Semantic single-tap callback routed by arbiter when allowed. */
    virtual void onInteractionTap(const InteractionEvent& event) { Q_UNUSED(event); }

    /** @brief Semantic double-tap callback routed by arbiter when allowed. */
    virtual void onInteractionDoubleTap(const InteractionEvent& event) { Q_UNUSED(event); }

    /** @brief Semantic long-press callback routed by arbiter when allowed. */
    virtual void onInteractionLongPress(const InteractionEvent& event) { Q_UNUSED(event); }

    /** @brief Semantic pinch callback routed by arbiter when allowed. */
    virtual void onInteractionPinch(const QPoint& centerGlobal, float factor) {
        Q_UNUSED(centerGlobal);
        Q_UNUSED(factor);
    }
};

#define InteractionTarget_iid "thermal_qt.ui.InteractionTarget/1.0"
Q_DECLARE_INTERFACE(InteractionTarget, InteractionTarget_iid)

#endif // UI_INTERACTION_TARGET_H
