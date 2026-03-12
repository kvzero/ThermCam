#include "ui_controller.h"
#include "ui/app.h"
#include "ui/gesture_recognizer.h"
#include "ui/views/base_view.h"
#include "core/event_bus.h"
#include "core/global_context.h"
#include "services/capture_service.h"

#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QMetaObject>
#include <QDebug>

const char* UIController::PROP_ALLOW_SLIDE_TRIGGER = "allowSlideTrigger";
const char* UIController::PROP_IS_INTERACTABLE     = "isInteractable";

UIController& UIController::instance() {
    static UIController inst;
    return inst;
}

UIController::UIController(QObject *parent) : QObject(parent) {
    GestureRecognizer::Config config;
    m_recognizer = new GestureRecognizer(config, this);

    m_shutdownTimer = new QTimer(this);
    m_shutdownTimer->setSingleShot(true);
    connect(m_shutdownTimer, &QTimer::timeout, this, &UIController::onKeyLongPressTimeout);

    connect(m_recognizer, &GestureRecognizer::touchesStarted,   this, &UIController::onTouchesStarted);
    connect(m_recognizer, &GestureRecognizer::touchesReleased,  this, &UIController::onTouchesReleased);
    connect(m_recognizer, &GestureRecognizer::tapDetected,       this, &UIController::onTapDetected);
    connect(m_recognizer, &GestureRecognizer::doubleTapDetected,this, &UIController::onDoubleTapDetected);
    connect(m_recognizer, &GestureRecognizer::longPressDetected, this, &UIController::onLongPressDetected);
    connect(m_recognizer, &GestureRecognizer::swipeUpdate,       this, &UIController::onSwipeUpdate);
    connect(m_recognizer, &GestureRecognizer::pinchUpdate,       this, &UIController::onPinchUpdate);
}

void UIController::init(App* app) {
    m_app = app;
    auto& bus = EventBus::instance();
    connect(&bus, &EventBus::rawKeySignal,   this, &UIController::handleRawKey);
    connect(&bus, &EventBus::rawTouchSignal, this, &UIController::handleRawTouch);
}

// ===================================================================
// HARDWARE EVENT HANDLERS
// ===================================================================

void UIController::handleRawKey(bool pressed) {
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

void UIController::onKeyLongPressTimeout() {
    if (!m_app) return;

    m_app->showConfirmDialog("CONFIRM POWER OFF?", []() {
        if (system("poweroff") != 0) {
            qWarning() << "[System] Shutdown command failed.";
        }
        QApplication::quit();
    });
}

void UIController::handleRawTouch(const QList<RawTouchPoint>& points) {
    const bool isTouching = !points.isEmpty();
    const bool isInitialContact = (isTouching && !m_wasTouching);

    if (isInitialContact) {
        const QPoint touchPosition(points.first().x, points.first().y);

        /**
         * STAGE 1: IDENTIFICATION
         * Find the most specific (deepest) widget at the contact point.
         */
        QWidget* hitWidget = findTargetWidget(touchPosition);
        if (!hitWidget || !hitWidget->property(PROP_IS_INTERACTABLE).toBool()) {
            if (auto* view = m_app->activeView()) {
                view->resetTransientUi();
            }
        }

        /**
         * STAGE 2: MODAL ARBITRATION
         * The modal overlay acts as a firewall for all underlying layers.
         */
        QWidget* activeModal = findTopModalWidget();
        if (activeModal) {
            bool isInsideModal = (hitWidget == activeModal || activeModal->isAncestorOf(hitWidget));
            if (!isInsideModal) hitWidget = activeModal;
        }

        /**
         * STAGE 3: PRIMARY INTENT LOCKDOWN
         * We capture the touch only if the widget identifies as a solid UI entity.
         * Widgets without 'isInteractable' are treated as transparent background.
         */
        if (hitWidget && hitWidget->property(PROP_IS_INTERACTABLE).toBool()) {
            injectMouseEvent(hitWidget, QEvent::MouseButtonPress, touchPosition);
            m_pressedWidget = hitWidget;
        } else {
            m_pressedWidget = nullptr;
        }
    }

    m_wasTouching = isTouching;
    m_recognizer->update(points);
}

// ===================================================================
// GESTURE SESSION ARBITRATION
// ===================================================================

void UIController::onTouchesStarted() {
    if (m_app && m_app->activeView()) {
        m_app->activeView()->onGestureStarted();
    }

    m_intentLocked = false;
    m_isGlobalGesture = false;
    clearHoverState();
}

void UIController::onSwipeUpdate(const QPoint& start, int dx, int dy) {
    if (!m_app) return;
    const QPoint currentPos = start + QPoint(dx, dy);

    // ---------------------------------------------------------------
    // PHASE 1: ACTIVE OWNER DELEGATION
    // ---------------------------------------------------------------
    if (m_pressedWidget) {
        const QMetaObject* meta = m_pressedWidget->metaObject();
        if (meta->indexOfMethod("handleInteractionUpdate(QPoint)") != -1) {

            QPoint localPos = m_pressedWidget->mapFromGlobal(currentPos);
            bool eventConsumed = false;

            QMetaObject::invokeMethod(m_pressedWidget, "handleInteractionUpdate",
                                      Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, eventConsumed),
                                      Q_ARG(QPoint, localPos));

            if (eventConsumed) return; // Locked owner continues to consume the event stream

            // The owner decided to release control
            injectMouseEvent(m_pressedWidget, QEvent::MouseButtonRelease, currentPos);
            m_pressedWidget = nullptr;
        }
    }

    // ---------------------------------------------------------------
    // PHASE 2: SYSTEM GESTURE ARBITRATION
    // ---------------------------------------------------------------
    if (!m_intentLocked) {
        const int screenH = GlobalContext::instance().screenSize().height();
        const int triggerZone = qRound(screenH * TOP_EDGE_RATIO);

        if (start.y() < triggerZone && dy > 0 && std::abs(dy) > std::abs(dx)) {
            m_isGlobalGesture = true;
            clearHoverState();
        }
        m_intentLocked = true;
    }

    if (m_isGlobalGesture) return;

    // ---------------------------------------------------------------
    // PHASE 3: THE SEARCHLIGHT EFFECT
    // ---------------------------------------------------------------
    // Detect interceptors that explicitly allow mid-swipe hijacking (e.g. Toasts).
    QWidget* interceptor = findTargetWidget(currentPos);
    if (interceptor && interceptor->property(PROP_ALLOW_SLIDE_TRIGGER).toBool()) {
        if (interceptor != m_pressedWidget) {
            if (m_pressedWidget) {
                injectMouseEvent(m_pressedWidget, QEvent::MouseButtonRelease, currentPos);
            }
            injectMouseEvent(interceptor, QEvent::MouseButtonPress, currentPos);
            m_pressedWidget = interceptor;
        }
        return;
    }

    // ---------------------------------------------------------------
    // PHASE 4: VIEW-LEVEL FALLBACK
    // ---------------------------------------------------------------
    if (findTopModalWidget()) return;

    if (auto* activeView = m_app->activeView()) {
        activeView->onGestureUpdate(start, dx, dy);
    }
}

void UIController::onTouchesReleased(const QPoint& start, int dx, int dy, float vx, float vy) {
    if (!m_app) return;
    const QPoint finalPos = start + QPoint(dx, dy);
    clearHoverState();

    if (!m_isGlobalGesture) {
        // Finalize background view physics
        if (auto* view = m_app->activeView()) {
            view->onGestureFinished(start, dx, dy, vx, vy);
        }

        // Finalize interaction for the current owner
        if (m_pressedWidget) {
            const QMetaObject* meta = m_pressedWidget->metaObject();
            if (meta->indexOfMethod("finalizeGesture(int)") != -1) {
                QMetaObject::invokeMethod(m_pressedWidget, "finalizeGesture",
                                          Qt::DirectConnection,
                                          Q_ARG(int, dy));
            } else {
                injectMouseEvent(m_pressedWidget, QEvent::MouseButtonRelease, finalPos);
            }
            m_pressedWidget = nullptr;
        }
    }
}

// ===================================================================
// SEMANTIC GESTURE HANDLERS
// ===================================================================

void UIController::onTapDetected(const QPoint& start, int dx, int dy) {
    const QPoint tapPos = start + QPoint(dx, dy);

    // Modal overlays handle their own release events (e.g., for dismissing).
    if (QWidget* modal = findTopModalWidget()) {
        injectMouseEvent(modal, QEvent::MouseButtonRelease, tapPos);
        return;
    }

    // Only forward Tap to the View if it wasn't consumed by a high-priority Widget.
    if (isViewInteractionAllowed(tapPos)) {
        if (m_app && m_app->activeView()) {
            m_app->activeView()->onTapDetected(tapPos);
        }
    }
}

void UIController::onDoubleTapDetected(const QPoint& start, int dx, int dy) {
    const QPoint pos = start + QPoint(dx, dy);

    if (isViewInteractionAllowed(pos)) {
        if (m_app && m_app->activeView()) {
            m_app->activeView()->onDoubleTapDetected(pos);
        }
    }
}

void UIController::onLongPressDetected(const QPoint& start) {
    if (isViewInteractionAllowed(start)) {
        if (m_app && m_app->activeView()) {
            m_app->activeView()->onLongPressDetected(start);
        }
    }
}

void UIController::onPinchUpdate(const QPoint& center, float factor) {
    if (isViewInteractionAllowed(center)) {
        if (m_app && m_app->activeView()) {
            m_app->activeView()->onPinchUpdate(center, factor);
        }
    }
}

// ===================================================================
// UTILITIES
// ===================================================================

QWidget* UIController::findTargetWidget(const QPoint& globalPos) {
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

void UIController::injectMouseEvent(QWidget* target, QEvent::Type type, const QPoint& globalPos) {
    if (!target) return;

    QPoint localPos = target->mapFromGlobal(globalPos);
    QMouseEvent mouseEvent(type, localPos, globalPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &mouseEvent);
}

void UIController::updateHoverState(const QPoint& globalPos) {
    QWidget* target = findTargetWidget(globalPos);

    if (target != m_currentHoveredWidget) {
        if (m_currentHoveredWidget) {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(m_currentHoveredWidget, &leave);
        }
        if (target) {
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(target, &enter);
        }
        m_currentHoveredWidget = target;
    }
}

void UIController::clearHoverState() {
    if (m_currentHoveredWidget) {
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(m_currentHoveredWidget, &leave);
        m_currentHoveredWidget = nullptr;
    }
}

QWidget* UIController::findTopModalWidget() {
    if (!m_app) return nullptr;

    const QObjectList& children = m_app->children();
    for (int i = children.size() - 1; i >= 0; --i) {
        QWidget* widget = qobject_cast<QWidget*>(children.at(i));
        if (widget && widget->isVisible() && widget->property("modalOverlay").toBool()) {
            return widget;
        }
    }
    return nullptr;
}

bool UIController::isViewInteractionAllowed(const QPoint& globalPos) {
    // 1. Z-Order Guard: If a modal dialog is visible, block all background interaction.
    if (findTopModalWidget()) return false;

    // 2. Interactive Guard: If the touch is on a primary control (isInteractable),
    // we block the view fallback to prevent "Ghost Clicks" or accidental background scrolling.
    QWidget* target = findTargetWidget(globalPos);
    if (target && target->property(PROP_IS_INTERACTABLE).toBool()) {
        return false;
    }

    return true;
}
