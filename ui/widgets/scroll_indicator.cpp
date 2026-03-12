#include "scroll_indicator.h"
#include <QEasingCurve>
#include <algorithm>

ScrollIndicator::ScrollIndicator(QObject* parent) : QObject(parent) {
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &ScrollIndicator::initiateFadeOut);

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnim->setDuration(m_cfg.FADE_DURATION_MS);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);
}

void ScrollIndicator::updateState(qreal currentScroll, qreal maxScroll) {
    m_currentScroll = currentScroll;
    m_maxScroll     = maxScroll;

    // Awaken the indicator immediately upon movement
    m_fadeAnim->stop();
    setOpacity(1.0);

    // Reset the idle countdown
    m_hideTimer->start(m_cfg.FADE_DELAY_MS);
}

void ScrollIndicator::initiateFadeOut() {
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}

void ScrollIndicator::setOpacity(qreal o) {
    if (qFuzzyCompare(m_opacity, o)) return;

    m_opacity = o;
    emit opacityChanged();
}

void ScrollIndicator::paint(QPainter& p, const QRect& viewRect) {
    // 1. Early exit if invisible or content doesn't require scrolling
    if (m_opacity <= 0.01 || m_maxScroll <= 0.0) return;

    const qreal viewportH = viewRect.height();
    const qreal contentH  = viewportH + m_maxScroll;

    // 2. Base Height Calculation
    qreal baseIndicatorH = viewportH * (viewportH / contentH);
    baseIndicatorH = std::max(baseIndicatorH, static_cast<qreal>(m_cfg.MIN_HEIGHT));

    const qreal availableTravel = viewportH - baseIndicatorH;
    qreal finalY = 0.0;
    qreal finalH = baseIndicatorH;

    // 3. Geometry computation with Overscroll Compression
    if (m_currentScroll < 0) {

        qreal overscroll = -m_currentScroll;

        qreal minSquish = static_cast<qreal>(m_cfg.WIDTH);

        qreal squishRatio = 100.0 / (100.0 + overscroll * m_cfg.COMPRESS_FACTOR);
        finalH = std::max(minSquish, baseIndicatorH * squishRatio);

        finalY = viewRect.top();
    }
    else if (m_currentScroll > m_maxScroll) {

        qreal overscroll = m_currentScroll - m_maxScroll;

        qreal minSquish = static_cast<qreal>(m_cfg.WIDTH);
        qreal squishRatio = 100.0 / (100.0 + overscroll * m_cfg.COMPRESS_FACTOR);
        finalH = std::max(minSquish, baseIndicatorH * squishRatio);

        finalY = viewRect.top() + viewRect.height() - finalH;
    }
    else {
        qreal progress = m_currentScroll / m_maxScroll;
        finalY = viewRect.top() + (progress * availableTravel);
    }

    // 4. Render to screen
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    QColor activeColor = m_cfg.COLOR_BASE;
    activeColor.setAlphaF(activeColor.alphaF() * m_opacity);

    p.setBrush(activeColor);
    p.setPen(Qt::NoPen);

    QRectF indicatorRect(viewRect.right() - m_cfg.WIDTH - m_cfg.MARGIN_RIGHT,
                         finalY,
                         m_cfg.WIDTH,
                         finalH);

    p.drawRoundedRect(indicatorRect, m_cfg.WIDTH / 2.0, m_cfg.WIDTH / 2.0);

    p.restore();
}
