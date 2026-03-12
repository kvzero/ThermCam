#include "toast_manager.h"
#include "core/global_context.h"

#include <QPainter>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QEasingCurve>
#include <cmath>

ToastManager::ToastManager(QWidget* parent) : QWidget(parent) {
    hide();
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", true);

    m_anim = new QPropertyAnimation(this, "offsetY", this);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setDuration(ANIM_DURATION_MS);

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &ToastManager::animateOut);
}

void ToastManager::showToast(const QString& msg, ToastLevel level) {
    m_queue.enqueue({msg, level});

    if (!m_isShowing) {
        processQueue();
    }
}

void ToastManager::processQueue() {
    if (m_queue.isEmpty()) {
        m_isShowing = false;
        hide();
        return;
    }

    m_current = m_queue.dequeue();
    m_isShowing = true;

    const int screenH = GlobalContext::instance().screenSize().height();
    m_contentH = qRound(screenH * HEIGHT_RATIO);
    m_baseY    = qRound(screenH * TOP_MARGIN_RATIO);

    setOffsetY(-m_contentH - m_baseY);

    show();
    raise();
    animateIn();
}

void ToastManager::animateIn() {
    m_anim->stop();
    disconnect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue);

    m_anim->setStartValue(m_offsetY);
    m_anim->setEndValue(0);
    m_anim->start();

    m_autoHideTimer->start(DISPLAY_DURATION_MS);
}

void ToastManager::animateOut() {
    m_anim->stop();
    connect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue, Qt::UniqueConnection);

    const int targetOutY = -m_contentH - m_baseY - DISMISS_OFFSET_PX;
    m_anim->setStartValue(m_offsetY);
    m_anim->setEndValue(targetOutY);
    m_anim->start();
}

bool ToastManager::handleInteractionUpdate(QPoint localPos) {
    m_autoHideTimer->stop();
    m_anim->stop();

    // Map the local coordinate back to global to get a stable, non-moving reference
    QPoint currentGlobalPos = this->mapToGlobal(localPos);

    // If the gesture entered from outside (Searchlight), sync the anchor on the first move
    if (m_isFirstGestureMove) {
        m_gestureStartY = currentGlobalPos.y();
        m_gestureStartOffsetY = m_offsetY;
        m_isFirstGestureMove = false;
    }

    // Calculate the TOTAL physical displacement since the start of the gesture
    int totalPhysicalDy = currentGlobalPos.y() - m_gestureStartY;

    int newVisualOffsetY = 0;

    // --- Absolute Mapping Logic ---
    if (totalPhysicalDy < 0) {
        // Upward movement: Linear mapping
        newVisualOffsetY = m_gestureStartOffsetY + totalPhysicalDy;
    } else {
        // Downward movement: Apply damping to the TOTAL displacement
        // This prevents jitter because the result is deterministic for any finger position
        const double dampedDelta = DAMPING_FACTOR * std::sqrt(static_cast<double>(totalPhysicalDy));
        newVisualOffsetY = m_gestureStartOffsetY + static_cast<int>(dampedDelta);
    }

    setOffsetY(newVisualOffsetY);

    return true;
}

void ToastManager::finalizeGesture(int /*unused*/) {
    // Calculate the final displacement based on the currentOffsetY vs startOffsetY
    int finalVisualDelta = m_offsetY - m_gestureStartOffsetY;

    // If the widget was pushed up beyond the threshold, dismiss it
    const int dismissThreshold = -qRound(m_contentH * DISMISS_THRESHOLD_RATIO);

    if (finalVisualDelta < dismissThreshold) {
        animateOut();
    } else {
        m_anim->stop();
        disconnect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue);

        m_anim->setStartValue(m_offsetY);
        m_anim->setEndValue(0);
        m_anim->start();

        m_autoHideTimer->start(RESUME_DURATION_MS);
    }
}

void ToastManager::setOffsetY(int y) {
    m_offsetY = y;
    move(0, m_baseY + m_offsetY);
    update();
}

QRect ToastManager::getVisualRect() const {
    const int pillW = qRound(width() * WIDTH_RATIO);
    return QRect((width() - pillW) / 2, 0, pillW, m_contentH);
}


void ToastManager::mousePressEvent(QMouseEvent* event) {
    // Record the baseline state when widget is grabbed
    m_gestureStartOffsetY = m_offsetY;

    // Capture the absolute global Y as the starting anchor
    m_gestureStartY = event->globalPos().y();
    m_isFirstGestureMove = false; // Mouse press already sets the anchor

    QWidget::mousePressEvent(event);
}

void ToastManager::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    const int screenH = GlobalContext::instance().screenSize().height();
    m_contentH = qRound(screenH * HEIGHT_RATIO);
    m_baseY    = qRound(screenH * TOP_MARGIN_RATIO);
}

void ToastManager::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int pillW = qRound(width() * WIDTH_RATIO);
    const int pillX = (width() - pillW) / 2;
    const QRect pillRect(pillX, 0, pillW, m_contentH);

    p.setBrush(QColor(25, 25, 25, 230));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(pillRect, m_contentH / 2.0, m_contentH / 2.0);

    if (m_current.level != ToastLevel::Info) {
        const QColor colorTop = (m_current.level == ToastLevel::Warning) ? QColor("#FFB74D") : QColor("#FF5252");
        const QColor colorBot = (m_current.level == ToastLevel::Warning) ? QColor("#E65100") : QColor("#B71C1C");

        const int barW = qMax(3, qRound(pillW * 0.015));
        const int barH = qRound(m_contentH * 0.5);
        const int barX = pillX + (m_contentH / 2);
        const int barY = (m_contentH - barH) / 2;
        const QRect barRect(barX, barY, barW, barH);

        p.setBrush(QColor(colorTop.red(), colorTop.green(), colorTop.blue(), 60));
        p.drawRoundedRect(barRect.adjusted(-2, -2, 2, 2), barW / 2.0, barW / 2.0);

        QLinearGradient grad(barRect.topLeft(), barRect.bottomLeft());
        grad.setColorAt(0, colorTop);
        grad.setColorAt(1, colorBot);
        p.setBrush(grad);
        p.drawRoundedRect(barRect, barW / 2.0, barW / 2.0);
    }

    p.setPen(Qt::white);
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(qRound(m_contentH * 0.4));
    p.setFont(font);

    const int textIndent = (m_current.level == ToastLevel::Info) ? (m_contentH / 2) : qRound(m_contentH * 0.85);
    const QRect textRect = pillRect.adjusted(textIndent, 0, -m_contentH / 2, 0);

    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_current.message);
}
