#include "capsule_button.h"
#include "core/event_bus.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDebug>

CapsuleButton::CapsuleButton(QWidget* parent) : QWidget(parent) {
    // Stage 3 Protocol: We are an entity, but we refuse "Searchlight" hijacking
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", false);

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout, this, &CapsuleButton::longPressed);

    // Initialize Animations with Cubic Out for "Premium" feel
    auto setupAnim = [this](QPropertyAnimation** anim, const char* prop) {
        *anim = new QPropertyAnimation(this, prop, this);
        (*anim)->setDuration(m_cfg.ANIM_DURATION_MS);
        (*anim)->setEasingCurve(QEasingCurve::OutCubic);
    };

    setupAnim(&m_topAnim, "topIconScale");
    setupAnim(&m_bottomAnim, "bottomIconScale");
    setupAnim(&m_glowAnim, "glowOpacity");
}

void CapsuleButton::mousePressEvent(QMouseEvent* event) {
    // Local capture starts. UIController will now delegate handleInteractionUpdate to us.
    m_isInside = true;
    m_glowPos = event->pos();

    m_glowAnim->stop();
    m_glowAnim->setEndValue(1.0);
    m_glowAnim->start();

    updateZone(event->pos());
    QWidget::mousePressEvent(event);
}

bool CapsuleButton::handleInteractionUpdate(QPoint localPos) {
    m_glowPos = localPos;
    bool wasInside = m_isInside;
    m_isInside = rect().contains(localPos);

    if (m_isInside) {
        if (!wasInside) {
            // Re-entry logic: Resume interaction
            m_glowAnim->stop();
            m_glowAnim->setEndValue(1.0);
            m_glowAnim->start();
            updateZone(localPos);
        } else {
            // Continuous tracking
            updateZone(localPos);
        }
    } else if (wasInside) {
        // Regret logic: User slid out, suspend state but keep ownership
        m_longPressTimer->stop();
        m_currentZone = ActiveZone::None;
        m_glowAnim->stop();
        m_glowAnim->setEndValue(0.0);
        m_glowAnim->start();
    }

    update();
    return true; // We maintain strict ownership until finger release
}

void CapsuleButton::updateZone(const QPoint& pos) {
    const int midY = height() / 2;
    const int deadzone = height() * m_cfg.HYSTERESIS_PCT;

    ActiveZone newZone = m_currentZone;

    // Hysteresis logic to prevent flickering at the center
    if (m_currentZone == ActiveZone::Settings) {
        if (pos.y() > (midY + deadzone)) newZone = ActiveZone::Gallery;
    } else if (m_currentZone == ActiveZone::Gallery) {
        if (pos.y() < (midY - deadzone)) newZone = ActiveZone::Settings;
    } else {
        newZone = (pos.y() < midY) ? ActiveZone::Settings : ActiveZone::Gallery;
    }

    if (newZone != m_currentZone) {
        m_currentZone = newZone;
        triggerPopAnimation(m_currentZone);
        startLongPressTimer();
    }
}

void CapsuleButton::triggerPopAnimation(ActiveZone zone) {
    auto runPop = [](QPropertyAnimation* anim, qreal start, qreal pop, qreal end) {
        anim->stop();
        anim->setKeyValues({ {0.0, start}, {0.4, pop}, {1.0, end} });
        anim->start();
    };

    if (zone == ActiveZone::Settings) {
        runPop(m_topAnim, m_topIconScale, m_cfg.SCALE_POP, m_cfg.SCALE_IDLE);
    } else {
        runPop(m_bottomAnim, m_bottomIconScale, m_cfg.SCALE_POP, m_cfg.SCALE_IDLE);
    }
}

void CapsuleButton::startLongPressTimer() {
    if (m_currentZone != ActiveZone::None) {
        m_longPressTimer->start(m_cfg.LONG_PRESS_MS);
    }
}

void CapsuleButton::finalizeGesture(int) {
    m_longPressTimer->stop();
    m_glowAnim->stop();
    m_glowAnim->setEndValue(0.0);
    m_glowAnim->start();

    m_currentZone = ActiveZone::None;
    m_isInside = false;
    update();
}

void CapsuleButton::longPressed() {
    emit EventBus::instance().hapticRequested(4);

    if (m_currentZone == ActiveZone::None) return;

    QRect halfRect;
    if (m_currentZone == ActiveZone::Settings) {
        halfRect = QRect(0, 0, width(), height() / 2);
    } else {
        halfRect = QRect(0, height() / 2, width(), height() / 2);
    }

    QPoint globalTopLeft = this->mapToGlobal(halfRect.topLeft());
    QRect globalAnchor(globalTopLeft, halfRect.size());

    if (m_currentZone == ActiveZone::Gallery) {
        emit EventBus::instance().galleryRequested(globalAnchor);
    } else if (m_currentZone == ActiveZone::Settings) {
        emit EventBus::instance().settingsRequested(globalAnchor);
    }
}

void CapsuleButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Apply collective opacity based on HUD's horizontal displacement
    p.setOpacity(m_contentsOpacity);

    // 1. Background: Frosted Glass Effect
    QPainterPath glassPath;
    glassPath.addRoundedRect(rect(), width() / 2.0, width() / 2.0);

    QLinearGradient bgGrad(rect().topLeft(), rect().bottomRight());
    bgGrad.setColorAt(0, m_cfg.BG_START);
    bgGrad.setColorAt(1, m_cfg.BG_END);
    p.fillPath(glassPath, bgGrad);

    // 2. Inner Stroke: Light refraction edge
    p.setPen(QPen(m_cfg.INNER_STROKE, 1.2));
    p.drawPath(glassPath);

    // 3. Dynamic Glow: Liquid light inside container
    if (m_glowOpacity > 0.01) {
        p.save();
        p.setClipPath(glassPath);
        p.setOpacity(m_glowOpacity * m_contentsOpacity);

        QRadialGradient glow(m_glowPos, width() * 1.0);
        glow.setColorAt(0.0, m_cfg.GLOW_CORE);
        glow.setColorAt(0.6, m_cfg.GLOW_HALO);
        glow.setColorAt(1.0, Qt::transparent);

        p.fillRect(rect(), glow);
        p.restore();
    }

    // 4. Icons: Adaptive state and scale
    p.setPen(Qt::white);

    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(width() * m_cfg.ICON_BASE_SIZE_RATIO);
    p.setFont(iconFont);

    auto drawIcon =[&](const QRect& area, const QString& icon, qreal scale, qreal opticalScale, bool active) {
        p.save();
        const qreal baseAlpha = active ? 1.0 : 0.6;
        p.setOpacity(baseAlpha * m_contentsOpacity);
        p.translate(area.center());

        qreal finalScale = scale * opticalScale;
        p.scale(finalScale, finalScale);

        QRect textRect(-area.width()/2, -area.height()/2, area.width(), area.height());

        p.setPen(m_cfg.SHADOW_COLOR);
        p.drawText(textRect.translated(1, 1), Qt::AlignCenter, icon);

        p.setPen(Qt::white);
        p.drawText(textRect, Qt::AlignCenter, icon);

        p.restore();
    };

    QRect topHalf(0, 0, width(), height() / 2);
    QRect botHalf(0, height() / 2, width(), height() / 2);

    drawIcon(topHalf,
             m_currentZone == ActiveZone::Settings ? m_cfg.ICON_SETTING_FILLED : m_cfg.ICON_SETTING_OUTLINE,
             m_topIconScale, m_cfg.ICON_SETTING_OPTICAL_SCALE, m_currentZone == ActiveZone::Settings);

    drawIcon(botHalf,
             m_currentZone == ActiveZone::Gallery ? m_cfg.ICON_GALLERY_FILLED : m_cfg.ICON_GALLERY_OUTLINE,
             m_bottomIconScale, m_cfg.ICON_GALLERY_OPTICAL_SCALE, m_currentZone == ActiveZone::Gallery);
}
