#include "ui/widgets/hud_container.h"
#include "ui/widgets/status_bar.h"
#include "ui/widgets/capsule_button.h"
#include "ui/widgets/mode_selector.h"
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QResizeEvent>
#include <cmath>

HudContainer::HudContainer(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);

    m_statusBar = new StatusBar(this);
    m_capsuleBtn = new CapsuleButton(this);
    m_modeSelector  = new ModeSelector(this);

    m_snapAnimation = new QPropertyAnimation(this, "pos", this);
    m_snapAnimation->setDuration(ANIMATION_DURATION_MS);
    m_snapAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void HudContainer::stopAnimations() {
    m_dragAnchorX = this->x();
    m_wasMoved = false;

    if (m_snapAnimation->state() == QAbstractAnimation::Running) {
        m_snapAnimation->stop();
        m_dragAnchorX = this->x(); // Synchronize with precise interruption point
    }
}

void HudContainer::followGesture(int dx) {
    if (!m_wasMoved && std::abs(dx) > GESTURE_DEADZONE_PX) {
        m_wasMoved = true;
    }

    int targetX = m_dragAnchorX + dx;

    // Constrain HUD within valid physical bounds [0, ScreenWidth]
    if (targetX < 0) targetX = 0;
    if (targetX > width()) targetX = width();

    this->move(targetX, 0);
}

void HudContainer::finalizeGesture(int finalDx, float vx) {

    const int screenW = width();
    const int currentX = this->x();

    // Intent Settlement Logic
    //  no active movement or flick is detected, we preserve the existing m_targetX.
    // This implements the "tap to interrupt, release to resume" requirement.If

    // Slow drag settles to the nearest stable state.
    if (m_wasMoved) {
        int threshold = qRound(screenW * SNAP_THRESHOLD_RATIO);

        if (std::abs(finalDx) > threshold) {
            m_targetX = (finalDx > 0) ? screenW : 0;
        } else {
            m_targetX = m_isHidden ? screenW : 0;
        }
    }

    // Trigger the physical motion engine
    int remainingDist = std::abs(m_targetX - currentX);
    int duration = ANIMATION_DURATION_MS;
    if (std::abs(vx) > 0.1f) {
        duration = qBound(100, qRound(remainingDist / std::abs(vx)), 350);
    }

    m_snapAnimation->setDuration(duration);
    m_snapAnimation->stop();
    m_snapAnimation->setStartValue(QPoint(currentX, 0));
    m_snapAnimation->setEndValue(QPoint(m_targetX, 0));
    m_snapAnimation->start();

    m_isHidden = (m_targetX > 0);
}

void HudContainer::resetPosition() {
    stopAnimations();
    m_targetX = 0;
    m_isHidden = false;
    this->move(0, 0);
}

void HudContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const int w = event->size().width();
    const int h = event->size().height();

    int barH = qRound(h * STATUS_BAR_H_RATIO);
    if (m_statusBar) m_statusBar->setGeometry(0, 0, w, barH);

    int capW = qRound(w * 0.15);
    int capH = qRound(h * 0.40);
    int margin = qRound(w * 0.05);
    if (m_capsuleBtn) {
        m_capsuleBtn->setGeometry(margin, h - capH - margin, capW, capH);
    }

    if (m_modeSelector) {
        int areaW = qRound(w * 0.33);
        int areaH = qRound(h * 0.4);
        m_modeSelector->setGeometry(w - areaW - margin, h - areaH - margin, areaW, areaH);
    }
}

/**
 * @brief Synchronizes sub-component transparency with the container's position.
 * Uses a Cubic-Out curve for a "Premium" vanishing feel.
 */
void HudContainer::moveEvent(QMoveEvent* event) {
    QWidget::moveEvent(event);

    const int screenWidth = this->width();
    if (screenWidth <= 0) return;

    // Calculate normalized progress (0.0 at home, 1.0 when fully hidden)
    qreal progress = static_cast<qreal>(this->x()) / screenWidth;
    progress = qBound(0.0, progress, 1.0);

    // Apply Aggressive Fade: Opacity = (1 - progress)^(kFadeExponent)
    // This starts fading immediately and reaches near-zero much earlier.
    constexpr qreal kFadeExponent = 1.5;
    qreal targetOpacity = std::pow(1.0 - progress, kFadeExponent);

    // Dispatch to registered self-drawing components
    if (m_statusBar)  m_statusBar->setContentsOpacity(targetOpacity);
    if (m_capsuleBtn) {
        // We cast to CapsuleButton to access the custom interface
        if (auto* btn = qobject_cast<CapsuleButton*>(m_capsuleBtn)) {
            btn->setContentsOpacity(targetOpacity);
        }
    }
    if (m_modeSelector) {
        m_modeSelector->setContentsOpacity(targetOpacity);
    }
}
