#include "gesture_recognizer.h"
#include "core/global_context.h"
#include <cmath>
#include <QDebug>

GestureRecognizer::GestureRecognizer(const Config& cfg, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_state(State_Idle)
    , m_vx(0.0f)
    , m_vy(0.0f)
{
    m_doubleTapTimer.invalidate();

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);

    connect(m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State_Pending) {
            m_state = State_LongPressed;
            emit longPressDetected(m_startPos);
        }
    });
}

void GestureRecognizer::reset() {
    m_state = State_Idle;
    m_vx = 0.0f;
    m_vy = 0.0f;
}

void GestureRecognizer::update(const QList<RawTouchPoint>& points) {
    int activeCount = 0;
    for (const auto& p : points) if (p.active) activeCount++;

    // Priority 1: Physical Conclusion (N -> 0)
    // Regardless of the semantic gesture, a complete lift-off triggers a physical settlement.
    if (activeCount == 0) {

        m_longPressTimer->stop();

        int finalDx = m_lastPos.x() - m_startPos.x();
        int finalDy = m_lastPos.y() - m_startPos.y();

        emit touchesReleased(m_startPos, finalDx, finalDy, m_vx, m_vy);

        // Evaluate Tap/Double-Tap only if the interaction was brief and static.
        if (m_state == State_Pending && m_timer.elapsed() < m_cfg.tapMaxTimeMs) {
            if (m_doubleTapTimer.isValid() &&
                m_doubleTapTimer.elapsed() < m_cfg.doubleTapIntervalMs &&
                calculateDistance(m_startPos, m_lastTapPos) < m_cfg.deadzone) {
                emit doubleTapDetected(m_startPos, finalDx, finalDy);
                m_doubleTapTimer.invalidate();
            } else {
                emit tapDetected(m_startPos, finalDx, finalDy);
                m_lastTapPos = m_startPos;
                m_doubleTapTimer.start();
            }
        }
        reset();
        return;
    }

    // Priority 2: Multi-Touch Arbitration (N >= 2)
    if (activeCount >= 2) {
        m_longPressTimer->stop();
        if (m_state != State_Pinching) {
            m_state = State_Pinching;
            // Capture initial distance to serve as the denominator for scaling.
            m_vx = calculateDistance(QPoint(points[0].x, points[0].y),
                                     QPoint(points[1].x, points[1].y));
        }
        handleMultiTouch(points);
    }
    // Priority 3: Single-Touch & Transition Handling (2 -> 1)
    else if (activeCount == 1) {
        const RawTouchPoint& p = points.first();
        QPoint current(p.x, p.y);

        // SILENT RE-ANCHORING:
        // When transitioning from Pinching back to Swiping, we reset the anchor
        // to the remaining finger's position and kill velocity. This prevents
        // coordinate jumps caused by the shift from a 'center-point' to a 'finger-point'.
        if (m_state == State_Pinching) {
            m_state = State_Swiping;
            m_startPos = current;
            m_lastPos = current;
            m_vx = 0.0f;
            m_vy = 0.0f;
        }
        handleSingleTouch(p);
    }
}

void GestureRecognizer::handleSingleTouch(const RawTouchPoint& p) {
    QPoint current(p.x, p.y);

    if (m_state == State_Idle) {
        m_state = State_Pending;
        m_startPos = current;
        m_lastPos = current;
        m_vx = 0.0f;
        m_vy = 0.0f;
        m_timer.start();
        m_longPressTimer->start(m_cfg.longPressTimeMs);
        emit touchesStarted(); // Immediate interrupt for UI animations.
        return;
    }

    int dx = current.x() - m_startPos.x();
    int dy = current.y() - m_startPos.y();
    float moveDist = calculateDistance(current, m_startPos);

    // State Promotion: Differentiate between static and dynamic intent.
    if ((m_state == State_Pending) && (moveDist > m_cfg.swipeThreshold)) {
            m_state = State_Swiping;
            m_longPressTimer->stop();
    }

    float dt = 1000.0f / GlobalContext::instance().refreshRate();
    if (dt <= 0) dt = 16.6f;

    float instVx = (float)(current.x() - m_lastPos.x()) / dt;
    float instVy = (float)(current.y() - m_lastPos.y()) / dt;

    m_vx = m_vx * (1.0f - m_cfg.velocityWeight) + instVx * m_cfg.velocityWeight;
    m_vy = m_vy * (1.0f - m_cfg.velocityWeight) + instVy * m_cfg.velocityWeight;
    m_lastPos = current;

    if (m_state == State_Swiping || m_state == State_LongPressed) {
        emit swipeUpdate(m_startPos, dx, dy);
    }

}

void GestureRecognizer::handleMultiTouch(const QList<RawTouchPoint>& points) {
    QPoint p1(points[0].x, points[0].y);
    QPoint p2(points[1].x, points[1].y);

    float currentDist = calculateDistance(p1, p2);
    QPoint center((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2);

    if (m_state != State_Pinching) {
        m_state = State_Pinching;
        m_vx = currentDist; // Re-purpose m_vx to store base distance, saving memory
        return;
    }

    if (m_vx > m_cfg.deadzone) {
        float factor = currentDist / m_vx;
        emit pinchUpdate(center, factor);
    }
}

float GestureRecognizer::calculateDistance(const QPoint& p1, const QPoint& p2) {
    float dx = p1.x() - p2.x();
    float dy = p1.y() - p2.y();
    return std::sqrt(dx * dx + dy * dy);
}
