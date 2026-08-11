#include "toast_manager.h"
#include "core/global_context.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QEasingCurve>
#include <QMouseEvent>
#include <cmath>

ToastManager::ToastManager(QWidget* parent) : QWidget(parent) {
    hide();
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    qApp->installEventFilter(this);

    m_anim = new QPropertyAnimation(this, "offsetY", this);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->setDuration(ANIM_DURATION_MS);

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &ToastManager::animateOut);
}

ToastManager::~ToastManager() {
    qApp->removeEventFilter(this);
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
        m_dragActive = false;
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
    m_dragActive = false;
    m_anim->stop();
    disconnect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue);

    m_anim->setStartValue(m_offsetY);
    m_anim->setEndValue(0);
    m_anim->start();

    m_autoHideTimer->start(DISPLAY_DURATION_MS);
}

void ToastManager::animateOut() {
    m_dragActive = false;
    m_anim->stop();
    connect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue, Qt::UniqueConnection);

    const int targetOutY = -m_contentH - m_baseY - DISMISS_OFFSET_PX;
    m_anim->setStartValue(m_offsetY);
    m_anim->setEndValue(targetOutY);
    m_anim->start();
}

bool ToastManager::eventFilter(QObject* watched, QEvent* event) {
    if (m_sendingSyntheticRelease || !m_isShowing || !isVisible()) {
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            visualRectGlobal().contains(mouse->globalPos())) {
            beginDrag(mouse->globalPos().y());
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (!(mouse->buttons() & Qt::LeftButton)) {
            return false;
        }

        if (m_dragActive) {
            updateDrag(mouse->globalPos().y());
            return true;
        }

        if (visualRectGlobal().contains(mouse->globalPos())) {
            releaseCurrentMouseReceiver(watched, mouse);
            beginDrag(mouse->globalPos().y());
            return true;
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && m_dragActive) {
            finishDrag();
            return true;
        }
    }

    return false;
}

void ToastManager::beginDrag(int globalY) {
    m_autoHideTimer->stop();
    m_anim->stop();
    disconnect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue);

    m_dragActive = true;
    m_dragStartGlobalY = globalY;
    m_dragStartOffsetY = m_offsetY;
}

void ToastManager::updateDrag(int globalY) {
    const int totalPhysicalDy = globalY - m_dragStartGlobalY;

    int newVisualOffsetY = 0;

    // --- Absolute Mapping Logic ---
    if (totalPhysicalDy < 0) {
        // Upward movement: Linear mapping
        newVisualOffsetY = m_dragStartOffsetY + totalPhysicalDy;
    } else {
        // Downward movement: Apply damping to the TOTAL displacement
        // This prevents jitter because the result is deterministic for any finger position
        const double dampedDelta = DAMPING_FACTOR * std::sqrt(static_cast<double>(totalPhysicalDy));
        newVisualOffsetY = m_dragStartOffsetY + static_cast<int>(dampedDelta);
    }

    setOffsetY(newVisualOffsetY);
}

void ToastManager::finishDrag() {
    if (!m_dragActive) return;

    m_dragActive = false;
    // Calculate the final displacement based on the currentOffsetY vs startOffsetY
    const int finalVisualDelta = m_offsetY - m_dragStartOffsetY;

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

void ToastManager::cancelDrag() {
    m_dragActive = false;
    m_anim->stop();
    disconnect(m_anim, &QPropertyAnimation::finished, this, &ToastManager::processQueue);

    m_anim->setStartValue(m_offsetY);
    m_anim->setEndValue(0);
    m_anim->start();

    if (isVisible()) {
        m_autoHideTimer->start(RESUME_DURATION_MS);
    }
}

void ToastManager::releaseCurrentMouseReceiver(QObject* receiver, const QMouseEvent* sourceEvent) {
    if (!receiver || !sourceEvent) return;

    // Outside-slide takeover happens mid mouse-grab. Finish the current Qt
    // receiver first so scrollers and pressed widgets do not lose release.
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                             sourceEvent->localPos(),
                             sourceEvent->windowPos(),
                             sourceEvent->screenPos(),
                             Qt::LeftButton,
                             sourceEvent->buttons() & ~Qt::LeftButton,
                             sourceEvent->modifiers());
    m_sendingSyntheticRelease = true;
    QCoreApplication::sendEvent(receiver, &releaseEvent);
    m_sendingSyntheticRelease = false;
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

QRect ToastManager::visualRectGlobal() const {
    const QRect visual = getVisualRect();
    return QRect(mapToGlobal(visual.topLeft()), visual.size());
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
