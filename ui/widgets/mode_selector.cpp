#include "mode_selector.h"
#include "core/event_bus.h"
#include "services/capture_service.h"
#include "hardware/hardware_manager.h"
#include "hardware/hmi/haptic_provider.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEasingCurve>

ModeSelector::ModeSelector(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);

    // Synchronize initial state with the logic service
    m_currentMode = CaptureService::instance().currentMode();
    m_pendingMode = m_currentMode;

    // Connect to the logic service for state-driven UI updates
    auto& svc = CaptureService::instance();
    connect(&svc, &CaptureService::modeChanged,      this, &ModeSelector::onModeChanged);
    connect(&svc, &CaptureService::recordingStarted, this, &ModeSelector::onRecordingStarted);
    connect(&svc, &CaptureService::recordingStopped, this, &ModeSelector::onRecordingStopped);
    connect(&svc, &CaptureService::recordingPaused,  this, &ModeSelector::onRecordingPaused);
    connect(&svc, &CaptureService::durationUpdated,  this, &ModeSelector::onDurationUpdated);
    connect(&svc, &CaptureService::blinkTick,        this, &ModeSelector::onBlinkTick);

    // Initialize high-performance property animations

    auto setup =[this](QPropertyAnimation** anim, const char* prop, int dur, QEasingCurve::Type curve) {
        *anim = new QPropertyAnimation(this, prop, this);
        (*anim)->setDuration(dur);
        (*anim)->setEasingCurve(curve);
    };

    setup(&m_vAnim,    "vStretch",    255, QEasingCurve::OutQuint);
    setup(&m_hAnim,    "hStretch",    350, QEasingCurve::OutQuad);
    setup(&m_glowAnim, "glowOpacity", 100, QEasingCurve::OutQuad);
    setup(&m_popAnim,  "iconPop",     200, QEasingCurve::OutQuad);

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout, this, &ModeSelector::longPressed);
}

void ModeSelector::collapse() {
    if (m_state == State::Picking) {
        m_vAnim->setDirection(QAbstractAnimation::Backward);
        m_vAnim->start();
        m_state = State::Normal;
        m_longPressTimer->stop();
        m_isPressed = false;
        m_isStickyPicking = false;
    }
}

void ModeSelector::setVStretch(qreal s) {
    m_vStretch = s;
    /**
     * Logic: Swap the displayed icon halfway through the reverse animation
     * to create a "morphing" visual effect between modes.
     */
    if (m_vStretch < 0.45 && m_currentMode != m_pendingMode) {
        m_currentMode = m_pendingMode;
    }
    update();
}

/** --- Interaction Logic (InteractionArbiter Implementation) --- */
void ModeSelector::mousePressEvent(QMouseEvent* event) {
    // Reset interaction gate.
    m_isPressed = false;

    if (!isPointInVisualArea(event->pos())) {
        // 如果在 Picking 状态下点到了自己的空白区域，视为“取消意图”，执行收回
        if (m_state == State::Picking) {
            collapse();
        }
        return;
    }

    m_isPressed = true; // Only authorized if hit-test passes
    m_glowPos = event->pos();

    m_glowAnim->stop();
    m_glowAnim->setEndValue(1.0);
    m_glowAnim->start();

    if (m_state == State::Picking) {
        CaptureMode otherMode = (m_currentMode == CaptureMode::Photo) ? CaptureMode::Video : CaptureMode::Photo;
        m_hoveredMode = (event->pos().y() < height() / 2) ? otherMode : m_currentMode;
        m_isStickyPicking = false;
    } else {
        m_popAnim->stop();
        m_popAnim->setKeyValues({{0.0, 1.0}, {0.4, 1.2}, {1.0, 1.0}});
        m_popAnim->start();
        m_longPressTimer->start(m_cfg.INTERNAL_LONG_PRESS_MS);
    }
    update();
}

bool ModeSelector::handleInteractionUpdate(QPoint localPos) {
    /**
     * Dynamic Hitbox.relinquishing control to the background view
     * immediately if the finger leaves the visual area.
     */
    if (m_state != State::Picking && !isPointInVisualArea(localPos)) {
        m_isPressed = false;
        m_longPressTimer->stop();
        return false;
    }

    m_glowPos = localPos;

    if (m_state == State::Picking) {
        CaptureMode otherMode = (m_currentMode == CaptureMode::Photo) ? CaptureMode::Video : CaptureMode::Photo;
        CaptureMode newHover = (localPos.y() < height() / 2) ? otherMode : m_currentMode;
        if (newHover != m_hoveredMode) {
            m_hoveredMode = newHover;
            m_popAnim->stop();
            m_popAnim->setKeyValues({{0.0, 1.0}, {0.4, 1.25}, {1.0, 1.0}});
            m_popAnim->start();
        }
    }
    update();
    return true;
}

void ModeSelector::finalizeGesture(int) {
    m_longPressTimer->stop();
    m_glowAnim->setEndValue(0.0);
    m_glowAnim->start();

    // Prevent execution if the press started outside the visual area.
    if (!m_isPressed) {
        update();
        return;
    }

    m_isPressed = false; // Consume the press event

    if (m_state == State::Picking) {
        bool modeChanged = (m_hoveredMode != m_currentMode);
        if (modeChanged || !m_isStickyPicking) {
            CaptureService::instance().setMode(m_hoveredMode);
            m_vAnim->setDirection(QAbstractAnimation::Backward);
            m_vAnim->start();
            m_state = State::Normal;
        } else {
            m_isStickyPicking = false;
        }
    }
    else if (m_state == State::Recording) {
        CaptureService::instance().togglePause();
    }

    update();
}
void ModeSelector::longPressed() {
    if (m_state != State::Normal) return;

    // Strong haptic feedback for long-press trigger
    EventBus::instance().hapticRequested(4);

    m_state = State::Picking;
    m_isStickyPicking = true;
    m_hoveredMode = m_currentMode;

    m_vAnim->setDirection(QAbstractAnimation::Forward);
    m_vAnim->setStartValue(0.0);
    m_vAnim->setEndValue(1.0);
    m_vAnim->start();
}

/** --- Service Signal Handlers --- */

void ModeSelector::onModeChanged(CaptureMode mode) {
    m_pendingMode = mode;
    // If we are not currently picking (e.g., external state change), update immediately
    if (m_state != State::Picking && m_vAnim->state() != QAbstractAnimation::Running) {
        m_currentMode = mode;
        update();
    }
}

void ModeSelector::onRecordingStarted() {
    m_state = State::Recording;
    m_timeStr = "00:00";
    m_isPaused = false;

    m_hAnim->setDirection(QAbstractAnimation::Forward);
    m_hAnim->setStartValue(0.0);
    m_hAnim->setEndValue(1.0);
    m_hAnim->start();
}

void ModeSelector::onRecordingStopped() {
    m_hAnim->setDirection(QAbstractAnimation::Backward);
    m_hAnim->start();
    m_state = State::Normal;
}

void ModeSelector::onRecordingPaused(bool paused) {
    m_isPaused = paused;
    update();
}

void ModeSelector::onDurationUpdated(const QString& time) {
    m_timeStr = time;
    update();
}

void ModeSelector::onBlinkTick(bool visible) {
    m_dotVisible = visible;
    update();
}

/** --- Rendering Engine --- */

void ModeSelector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_contentsOpacity);

    /**
     * Geometry: The widget is anchored to the bottom-right of its allocation.
     * baseCircleSize defines the "Normal" circle diameter.
     */
    const qreal baseSize = qMin(width(), height()) * 0.5;
    const qreal curW = baseSize + (width() - baseSize) * m_hStretch;
    const qreal curH = baseSize + (height() - baseSize) * m_vStretch;

    QRectF bgRect(width() - curW, height() - curH, curW, curH);
    qreal radius = baseSize / 2.0;

    // Layer 1: Frosted Glass Background
    QPainterPath path;
    path.addRoundedRect(bgRect, radius, radius);

    QLinearGradient bg(bgRect.topLeft(), bgRect.bottomRight());
    bg.setColorAt(0, m_cfg.BG_START); bg.setColorAt(1, m_cfg.BG_END);
    p.fillPath(path, bg);

    // Layer 2: Interactive Glow
    if (m_glowOpacity > 0.01) {
        p.save();
        p.setClipPath(path);

        p.setOpacity(m_glowOpacity * m_contentsOpacity);

        qreal glowRadius = baseSize * 1.0;
        QRadialGradient g(m_glowPos, glowRadius);

        g.setColorAt(0.0, m_cfg.GLOW_CORE);
        g.setColorAt(0.6, m_cfg.GLOW_HALO);
        g.setColorAt(1.0, Qt::transparent);

        p.fillRect(rect(), g);
        p.restore();
    }

    // Layer 3: Inner Refraction Stroke
    p.setPen(QPen(m_cfg.INNER_STROKE, 1.2));
    p.drawPath(path);

    // Layer 4: Content Compositing
    if (m_hStretch > 0.4) {
        drawRecordingInfo(p, bgRect.toRect());
    }
    else if (m_vStretch > 0.1) {
        /**
         * Layout: Bottom slot is fixed to m_currentMode for tactile continuity.
         * Top slot reveals the alternative option as the widget expands upward.
         */
        CaptureMode bottomMode = m_currentMode;
        CaptureMode topMode = (m_currentMode == CaptureMode::Photo) ? CaptureMode::Video : CaptureMode::Photo;

        QRect topHalf(bgRect.x(), bgRect.y(), curW, curH / 2);
        QRect botHalf(bgRect.x(), bgRect.y() + curH / 2, curW, curH / 2);

        bool tActive = (m_hoveredMode == topMode);
        drawIcon(p, topHalf, topMode, tActive, (tActive ? m_iconPop : 1.0));

        bool bActive = (m_hoveredMode == bottomMode);
        drawIcon(p, botHalf, bottomMode, bActive, (bActive ? m_iconPop : 1.0));
    }
    else {
        drawIcon(p, bgRect.toRect(), m_currentMode, m_isPressed, m_iconPop);
    }
}

void ModeSelector::drawIcon(QPainter& p, const QRect& r, CaptureMode mode, bool active, qreal scale) {
    p.save();
    p.setOpacity(active ? 1.0 : 0.6);

    QFont f("tabler-icons");
    f.setPixelSize(r.height() * 0.55);
    p.setFont(f);

    QString txt;
    if (mode == CaptureMode::Photo) {
        txt = active ? m_cfg.ICON_CAM_FILL : m_cfg.ICON_CAM_OUT;
    } else {
        if (active) {
            uint ucs4 = m_cfg.ICON_VID_HEX;
            txt = QString::fromUcs4(&ucs4, 1); // Handles 5-digit hex code
        } else {
            txt = m_cfg.ICON_VID_OUT;
        }
    }

    p.translate(r.center());
    p.scale(scale, scale);
    QRect textR(-r.width()/2, -r.height()/2, r.width(), r.height());

    // Layer 1: Contrast Shadow (1px offset)
    p.setPen(m_cfg.SHADOW_COLOR);
    p.drawText(textR.translated(1, 1), Qt::AlignCenter, txt);

    // Layer 2: Main White Content
    p.setPen(Qt::white);
    p.drawText(textR, Qt::AlignCenter, txt);
    p.restore();
}

void ModeSelector::drawRecordingInfo(QPainter& p, const QRect& r) {
    // Red Status Dot
    if (m_dotVisible || m_isPaused) {
        p.save();
        qreal dotR = r.height() * 0.15;
        QPointF dotCenter(r.x() + r.height() * 0.4, r.center().y());

        p.setBrush(m_isPaused ? Qt::gray : m_cfg.RECORD_RED);
        p.setPen(Qt::NoPen);
        p.drawEllipse(dotCenter, dotR, dotR);
        p.restore();
    }

    // Monospaced Timestamp (Reduces jitter)
    p.save();
    QFont f("Roboto");
    f.setPixelSize(r.height() * 0.45);
    f.setBold(true);
    f.setFixedPitch(true);
    p.setFont(f);

    QRect tRect = r.adjusted(r.height() * 0.7, 0, 0, 0);

    // Contrast Layer
    p.setPen(m_cfg.SHADOW_COLOR);
    p.drawText(tRect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, m_timeStr);

    // Primary Content Layer
    p.setPen(m_isPaused ? Qt::lightGray : Qt::white);
    p.drawText(tRect, Qt::AlignLeft | Qt::AlignVCenter, m_timeStr);
    p.restore();
}


bool ModeSelector::isPointInVisualArea(const QPoint& pos) {
    const qreal baseSize = width() * 0.5;
    const qreal curW = baseSize + (width() - baseSize) * m_hStretch;
    const qreal curH = baseSize + (height() - baseSize) * m_vStretch;

    QRectF visualRect(width() - curW, height() - curH, curW, curH);

    // Expand the judgment area
    return visualRect.adjusted(-20, -20, 20, 20).contains(pos);
}
