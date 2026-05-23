#include "interaction_arbiter.h"
#include "ui/app.h"
#include "ui/gesture_recognizer.h"
#include "ui/views/base_view.h"
#include "core/event_bus.h"
#include "core/global_context.h"
#include "services/capture_service.h"

#include <QApplication>
#include <QDateTime>
#include <QTimer>
#include <QDebug>

const char* InteractionArbiter::PROP_ALLOW_SLIDE_TRIGGER = "allowSlideTrigger";
const char* InteractionArbiter::PROP_IS_INTERACTABLE     = "isInteractable";

InteractionArbiter& InteractionArbiter::instance() {
    static InteractionArbiter inst;
    return inst;
}

InteractionArbiter::InteractionArbiter(QObject *parent) : QObject(parent) {
    GestureRecognizer::Config config;
    m_recognizer = new GestureRecognizer(config, this);

    m_shutdownTimer = new QTimer(this);
    m_shutdownTimer->setSingleShot(true);
    connect(m_shutdownTimer, &QTimer::timeout, this, &InteractionArbiter::onKeyLongPressTimeout);

    connect(m_recognizer, &GestureRecognizer::touchesStarted, this, &InteractionArbiter::onRecognizerTouchesStarted);
    connect(m_recognizer, &GestureRecognizer::touchesReleased, this, &InteractionArbiter::onRecognizerTouchesReleased);
    connect(m_recognizer, &GestureRecognizer::tapDetected, this, &InteractionArbiter::onRecognizerTap);
    connect(m_recognizer, &GestureRecognizer::doubleTapDetected, this, &InteractionArbiter::onRecognizerDoubleTap);
    connect(m_recognizer, &GestureRecognizer::longPressDetected, this, &InteractionArbiter::onRecognizerLongPress);
    connect(m_recognizer, &GestureRecognizer::swipeUpdate, this, &InteractionArbiter::onRecognizerSwipeUpdate);
    connect(m_recognizer, &GestureRecognizer::pinchUpdate, this, &InteractionArbiter::onRecognizerPinchUpdate);
}

void InteractionArbiter::init(App* app) {
    m_app = app;
    auto& bus = EventBus::instance();
    connect(&bus, &EventBus::rawKeySignal,   this, &InteractionArbiter::handleRawKey);
    connect(&bus, &EventBus::rawTouchSignal, this, &InteractionArbiter::handleRawTouch);
}

void InteractionArbiter::cancelTouchSession() {
    if (m_ownerTarget) {
        m_ownerTarget->onInteractionCancel();
    }
    clearOwner();

    m_intentLocked = false;
    m_isGlobalGesture = false;
    m_activePointerCount = 0;

    if (m_recognizer) {
        if (m_wasTouching) {
            m_recognizer->cancel();
        } else {
            m_recognizer->reset();
        }
    }
}

// ===================================================================
// HARDWARE EVENT HANDLERS
// ===================================================================

void InteractionArbiter::handleRawKey(bool pressed) {
    if (!m_app) return;

    if (pressed) {

        if (auto* view = m_app->activeView()) view->resetTransientUi();
        m_shutdownTimer->start(SHUTDOWN_HOLD_MS);

    } else {

        if (m_shutdownTimer->isActive()) {
            m_shutdownTimer->stop();
            if (auto* view = m_app->activeView()) view->handleKeyShortPress();
        }

    }
}

void InteractionArbiter::onKeyLongPressTimeout() {
    if (!m_app) return;

    m_app->showTextModal("CONFIRM POWER OFF?", []() {
        if (system("poweroff") != 0) {
            qWarning() << "[System] Shutdown command failed.";
        }
        QApplication::quit();
    });
}

void InteractionArbiter::handleRawTouch(const QList<RawTouchPoint>& points) {
    const bool isTouching = !points.isEmpty();
    const bool isInitialContact = (isTouching && !m_wasTouching);
    m_activePointerCount = points.size();

    if (isInitialContact) {
        const QPoint touchPosition(points.first().x, points.first().y);

        /* Stage 1: identify the deepest hit target at contact position. */
        QWidget* hitWidget = findTargetWidget(touchPosition);
        if (!hitWidget || !hitWidget->property(PROP_IS_INTERACTABLE).toBool()) {
            if (auto* view = m_app->activeView()) {
                view->resetTransientUi();
            }
        }

        /*
         * Stage 2: lock the initial owner.
         * Non-interactable hits are treated as transparent and routed to view.
         */
        if (hitWidget && hitWidget->property(PROP_IS_INTERACTABLE).toBool()) {
            if (InteractionTarget* target = targetFromWidget(hitWidget)) {
                setOwner(target, hitWidget, touchPosition);
            } else if (BaseView* view = activeViewTarget()) {
                qWarning() << "[Interaction] Interactable widget without InteractionTarget, fallback to view owner.";
                setOwner(view, view, touchPosition);
            } else {
                clearOwner();
            }
        } else {
            if (BaseView* view = activeViewTarget()) {
                setOwner(view, view, touchPosition);
            } else {
                clearOwner();
            }
        }
    }

    m_wasTouching = isTouching;
    m_recognizer->update(points);
}

// ===================================================================
// GESTURE SESSION ARBITRATION
// ===================================================================

void InteractionArbiter::onRecognizerTouchesStarted() {
    m_intentLocked = false;
    m_isGlobalGesture = false;
}

void InteractionArbiter::onRecognizerSwipeUpdate(const QPoint& start, int dx, int dy) {
    if (!m_app) return;
    const QPoint currentPos = start + QPoint(dx, dy);
    BaseView* const activeView = activeViewTarget();
    const bool hasActiveView = (activeView != nullptr);
    bool phase1UpdatedCurrentOwner = false;
    bool phase1OwnerKept = false;
    bool phase1OwnerWasView = false;

    /* Phase 1: update active owner if any. */
    if (m_ownerTarget) {
        phase1OwnerWasView = (hasActiveView && m_ownerWidget == activeView);
        const InteractionEvent event =
            buildEventFor(m_ownerWidget, m_ownerStartGlobal, m_ownerPrevGlobal, currentPos, QPointF());
        const InteractionUpdateDecision decision = m_ownerTarget->onInteractionUpdate(event);
        phase1UpdatedCurrentOwner = true;
        m_ownerPrevGlobal = currentPos;

        if (decision == InteractionUpdateDecision::KeepOwner) {
            phase1OwnerKept = true;
            if (!phase1OwnerWasView) {
                return;
            }
        } else {
            // Owner released ownership mid-gesture; route switch uses cancel semantics.
            // End semantics are emitted only on physical touchesReleased().
            m_ownerTarget->onInteractionCancel();
            clearOwner();
        }
    }

    /* Phase 2: decide whether this swipe becomes the global pull-down route. */
    if (!m_intentLocked) {
        const int screenH = GlobalContext::instance().screenSize().height();
        const int triggerZone = qRound(screenH * TOP_EDGE_RATIO);

        if (start.y() < triggerZone && dy > 0 && std::abs(dy) > std::abs(dx)) {
            m_isGlobalGesture = true;
        }
        m_intentLocked = true;
    }

    if (m_isGlobalGesture) {
        if (m_ownerTarget && phase1OwnerWasView) {
            m_ownerTarget->onInteractionCancel();
            clearOwner();
        }
        return;
    }

    /* Phase 3: allow interceptors that opt into mid-swipe takeover. */
    QWidget* interceptor = findTargetWidget(currentPos);
    if (interceptor && interceptor->property(PROP_ALLOW_SLIDE_TRIGGER).toBool()) {
        if (interceptor != m_ownerWidget) {
            if (m_ownerTarget) {
                m_ownerTarget->onInteractionCancel();
                clearOwner();
            }
            if (InteractionTarget* target = targetFromWidget(interceptor)) {
                setOwner(target, interceptor, currentPos);
            }
        }
        return;
    }

    /* Phase 4: fallback to active view owner. */
    if (!m_ownerTarget) {
        if (hasActiveView) {
            // Keep the original session anchor so edge/back thresholds remain continuous
            // when ownership transfers from a widget back to the view.
            setOwner(activeView, activeView, start);
            m_ownerPrevGlobal = currentPos;
        }
    }

    if (m_ownerTarget) {
        if (phase1UpdatedCurrentOwner && phase1OwnerKept &&
            hasActiveView && m_ownerWidget == activeView) {
            return;
        }

        const InteractionEvent event =
            buildEventFor(m_ownerWidget, m_ownerStartGlobal, m_ownerPrevGlobal, currentPos, QPointF());
        const InteractionUpdateDecision decision = m_ownerTarget->onInteractionUpdate(event);
        m_ownerPrevGlobal = currentPos;
        if (decision == InteractionUpdateDecision::ReleaseOwner) {
            // Owner released ownership mid-gesture; route switch uses cancel semantics.
            // End semantics are emitted only on physical touchesReleased().
            m_ownerTarget->onInteractionCancel();
            clearOwner();
        }
    }
}

void InteractionArbiter::onRecognizerTouchesReleased(const QPoint& start, int dx, int dy, float vx, float vy) {
    if (!m_app) return;
    const QPoint finalPos = start + QPoint(dx, dy);

    if (!m_isGlobalGesture && m_ownerTarget) {
        // Physical touch release is the only path that emits end semantics.
        const InteractionEvent event =
            buildEventFor(m_ownerWidget, m_ownerStartGlobal, m_ownerPrevGlobal, finalPos, QPointF(vx, vy));
        m_ownerTarget->onInteractionEnd(event);
    }
    clearOwner();
    m_activePointerCount = 0;
}

// ===================================================================
// SEMANTIC GESTURE HANDLERS
// ===================================================================

void InteractionArbiter::onRecognizerTap(const QPoint& start, int dx, int dy) {
    const QPoint tapPos = start + QPoint(dx, dy);

    // Only forward Tap to the View when the hit target is not an interactable widget.
    if (isViewInteractionAllowed(tapPos)) {
        if (BaseView* view = activeViewTarget()) {
            view->onInteractionTap(buildSemanticEvent(tapPos));
        }
    }
}

void InteractionArbiter::onRecognizerDoubleTap(const QPoint& start, int dx, int dy) {
    const QPoint pos = start + QPoint(dx, dy);

    if (isViewInteractionAllowed(pos)) {
        if (BaseView* view = activeViewTarget()) {
            view->onInteractionDoubleTap(buildSemanticEvent(pos));
        }
    }
}

void InteractionArbiter::onRecognizerLongPress(const QPoint& start) {
    if (isViewInteractionAllowed(start)) {
        if (BaseView* view = activeViewTarget()) {
            view->onInteractionLongPress(buildSemanticEvent(start));
        }
    }
}

void InteractionArbiter::onRecognizerPinchUpdate(const QPoint& center, float factor) {
    if (isViewInteractionAllowed(center)) {
        if (BaseView* view = activeViewTarget()) {
            view->onInteractionPinch(center, factor);
        }
    }
}

// ===================================================================
// UTILITIES
// ===================================================================

BaseView* InteractionArbiter::activeViewTarget() const {
    if (!m_app) return nullptr;
    return m_app->activeView();
}

InteractionTarget* InteractionArbiter::targetFromWidget(QWidget* widget) const {
    if (!widget) return nullptr;
    return qobject_cast<InteractionTarget*>(widget);
}

QWidget* InteractionArbiter::findTargetWidget(const QPoint& globalPos) {
    if (!m_app) return nullptr;

    QWidget* target = m_app->findWidgetAt(globalPos);
    if (target) return target;

    // Fuzzy hit test for small tactile controls
    const QList<QPoint> expansionOffsets = {
        {0, -HIT_EXPANSION_PX}, {0, HIT_EXPANSION_PX},
        {-HIT_EXPANSION_PX, 0}, {HIT_EXPANSION_PX, 0}
    };

    for (const auto& offset : expansionOffsets) {
        target = m_app->findWidgetAt(globalPos + offset);
        if (target) return target;
    }
    return nullptr;
}

void InteractionArbiter::setOwner(InteractionTarget* target,
                                  QWidget* widget,
                                  const QPoint& anchorGlobal) {
    if (!target || !widget) {
        clearOwner();
        return;
    }

    m_ownerTarget = target;
    m_ownerWidget = widget;
    m_ownerStartGlobal = anchorGlobal;
    m_ownerPrevGlobal = anchorGlobal;

    const InteractionEvent event =
        buildEventFor(m_ownerWidget, m_ownerStartGlobal, m_ownerPrevGlobal, anchorGlobal, QPointF());
    m_ownerTarget->onInteractionBegin(event);
}

void InteractionArbiter::clearOwner() {
    m_ownerTarget = nullptr;
    m_ownerWidget = nullptr;
    m_ownerStartGlobal = QPoint();
    m_ownerPrevGlobal = QPoint();
}

InteractionEvent InteractionArbiter::buildEventFor(QWidget* widget,
                                                   const QPoint& startGlobal,
                                                   const QPoint& previousGlobal,
                                                   const QPoint& currentGlobal,
                                                   const QPointF& velocityPxPerMs) const {
    const auto mapLocal =[widget](const QPoint& globalPoint) {
        return widget ? widget->mapFromGlobal(globalPoint) : globalPoint;
    };

    InteractionEvent event;
    event.startGlobal = startGlobal;
    event.previousGlobal = previousGlobal;
    event.currentGlobal = currentGlobal;
    event.deltaFromStartGlobal = currentGlobal - startGlobal;
    event.deltaFromPreviousGlobal = currentGlobal - previousGlobal;
    event.startLocal = mapLocal(startGlobal);
    event.previousLocal = mapLocal(previousGlobal);
    event.currentLocal = mapLocal(currentGlobal);
    event.deltaFromStartLocal = event.currentLocal - event.startLocal;
    event.deltaFromPreviousLocal = event.currentLocal - event.previousLocal;
    event.velocityPxPerMs = velocityPxPerMs;
    event.pointerCount = qMax(1, m_activePointerCount);
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();
    return event;
}

InteractionEvent InteractionArbiter::buildSemanticEvent(const QPoint& globalPos) const {
    QWidget* viewWidget = activeViewTarget();
    return buildEventFor(viewWidget, globalPos, globalPos, globalPos, QPointF());
}

bool InteractionArbiter::isViewInteractionAllowed(const QPoint& globalPos) {
    // Interactive Guard: If the touch is on a primary control (isInteractable),
    // we block the view fallback to prevent "Ghost Clicks" or accidental background scrolling.
    QWidget* target = findTargetWidget(globalPos);
    if (target && target->property(PROP_IS_INTERACTABLE).toBool()) {
        return false;
    }

    return true;
}
