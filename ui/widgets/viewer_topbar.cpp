#include "viewer_topbar.h"
#include "core/global_context.h"
#include <QEasingCurve>
#include <QDateTime>
#include <QPainterPath>
#include <QMouseEvent>

ViewerTopBar::ViewerTopBar(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &ViewerTopBar::initiateFadeOut);

    m_fadeAnim = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnim->setDuration(m_cfg.FADE_DURATION_MS);
    m_fadeAnim->setEasingCurve(QEasingCurve::InOutQuad);

    auto setupAnim = [this](QPropertyAnimation** anim, const char* prop) {
        *anim = new QPropertyAnimation(this, prop, this);
        (*anim)->setDuration(150);
        (*anim)->setEasingCurve(QEasingCurve::OutQuad);
    };

    setupAnim(&m_backGlowAnim, "backGlow");
    setupAnim(&m_deleteGlowAnim, "deleteGlow");
    setupAnim(&m_timeGlowAnim, "timeGlow");
}

void ViewerTopBar::show() {
    QWidget::show();
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();
    m_hideTimer->start(m_cfg.FADE_DELAY_MS);
}

void ViewerTopBar::hideImmediate() {
    m_hideTimer->stop();
    m_fadeAnim->stop();
    m_opacity = 0.0;
}

void ViewerTopBar::updateInfo(const MediaFileInfo& info) {
    m_currentInfo = info;
    show(); // Auto-awaken when flipping to a new image
}

void ViewerTopBar::initiateFadeOut() {
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(m_opacity);
    m_fadeAnim->setEndValue(0.0);
    m_fadeAnim->start();
}



void ViewerTopBar::triggerGlowAnimation(QPropertyAnimation* anim, bool active) {
    qreal target = active ? 1.0 : 0.0;
    if (anim->endValue().toReal() != target) {
        anim->stop();
        anim->setEndValue(target);
        anim->start();
    }
}

void ViewerTopBar::mousePressEvent(QMouseEvent* event) {
    handleInteractionUpdate(event->pos());
    QWidget::mousePressEvent(event);
}

bool ViewerTopBar::handleInteractionUpdate(QPoint localPos) {
    if (m_opacity < 0.1) return false;

    m_lastGlowPos = localPos;

    // Expand hitboxes slightly for touch friendliness
    qreal expand = m_backRect.height() * 0.3;
    bool newHoverBack = m_backRect.adjusted(-expand, -expand, expand, expand).contains(localPos);
    bool newHoverDel = m_deleteRect.adjusted(-expand, -expand, expand, expand).contains(localPos);
    bool newHoverTime = m_timeRect.adjusted(-expand, -expand, expand, expand).contains(localPos);


    //  Trigger glow animations dynamically based on hit testing
    triggerGlowAnimation(m_backGlowAnim, newHoverBack);
    triggerGlowAnimation(m_deleteGlowAnim, newHoverDel);
    triggerGlowAnimation(m_timeGlowAnim, newHoverTime);


     m_hoverBack = newHoverBack;
     m_hoverDelete = newHoverDel;
     m_hoverTime = newHoverTime;
     update();


    // Keep the bar alive while interacting
    if (newHoverBack || newHoverDel || newHoverTime) {
        m_hideTimer->start(m_cfg.FADE_DELAY_MS);
        emit interactionActive();
        return true;
    }
    return false;
}

void ViewerTopBar::finalizeGesture(int) {
    // Turn off all glows
    triggerGlowAnimation(m_backGlowAnim, false);
    triggerGlowAnimation(m_deleteGlowAnim, false);
    triggerGlowAnimation(m_timeGlowAnim, false);

    if (m_hoverBack) {
        QTimer::singleShot(100, this, &ViewerTopBar::backRequested);
    } else if (m_hoverDelete) {
        QTimer::singleShot(100, this, &ViewerTopBar::deleteRequested);
    }
    // Time label is usually purely informational, so no action on hoverTime

    m_hoverBack = false;
    m_hoverDelete = false;
    m_hoverTime = false;
    update();
}

void ViewerTopBar::drawGlowEffect(QPainter& p, const QPainterPath& clipPath, qreal glowAlpha) {
    if (glowAlpha <= 0.01) return;

    p.save();
    p.setClipPath(clipPath);

    // Scale opacity by parent global opacity
    p.setOpacity(glowAlpha * m_opacity);

    // Create a smooth radial gradient originating directly beneath the finger
    qreal radius = clipPath.boundingRect().height() * 1.5;
    QRadialGradient glow(m_lastGlowPos, radius);
    glow.setColorAt(0.0, QColor(255, 255, 255, 80));  // Core glow
    glow.setColorAt(1.0, Qt::transparent);            // Fade out

    p.fillPath(clipPath, glow);
    p.restore();
}

void ViewerTopBar::paintEvent(QPaintEvent*) {
    if (m_opacity <= 0.01) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);

    const int screenH = GlobalContext::instance().screenSize().height();
    const int screenW = width();

    const qreal h = screenH * (m_cfg.HEIGHT_RATIO / 100.0);
    const qreal margin = screenW * (m_cfg.MARGIN_RATIO / 100.0);
    const qreal btnSize = h;

    // Background Gradient (Darken the top edge for contrast)
    QRectF bgRect(0, 0, screenW, height());
    QLinearGradient grad(bgRect.topLeft(), bgRect.bottomLeft());
    grad.setColorAt(0.0, QColor(0, 0, 0, 80));
    grad.setColorAt(1.0, Qt::transparent);
    p.fillRect(bgRect, grad);

    // Geometry Definitions for Side Buttons
    m_backRect = QRectF(margin, margin, btnSize, btnSize);
    m_deleteRect = QRectF(screenW - margin - btnSize, margin, btnSize, btnSize);

    //  Dynamic Time Capsule Geometry
    QString dateStr = QDateTime::fromMSecsSinceEpoch(m_currentInfo.timestamp).toString("yyyy.MM.dd  HH:mm");
    QFont textFont("Roboto");
    textFont.setPixelSize(qRound(btnSize * 0.4));
    textFont.setBold(true);
    QFontMetricsF fm(textFont);

    qreal textWidth = fm.horizontalAdvance(dateStr);
    qreal timeBarWidth = textWidth + btnSize;
    qreal timeBarX = (screenW - timeBarWidth) / 2.0;

    m_timeRect = QRectF(timeBarX, margin, timeBarWidth, btnSize);

    // Font Setup for Icons
    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(btnSize * 0.6));

    QPen strokePen(m_cfg.STROKE_COLOR, 1.2);

    // 1. Draw Back Button
    QPainterPath backPath;
    backPath.addEllipse(m_backRect);
    p.fillPath(backPath, m_hoverBack ? QColor(80, 80, 80, 200) : m_cfg.BG_COLOR);
    drawGlowEffect(p, backPath, m_backGlow);
    p.setPen(strokePen);
    p.drawPath(backPath);

    p.setPen(Qt::white);
    p.setFont(iconFont);
    p.drawText(m_backRect, Qt::AlignCenter, m_cfg.ICON_BACK);

    // 2. Draw Delete Button
    QPainterPath delPath;
    delPath.addEllipse(m_deleteRect);
    p.fillPath(delPath, m_hoverDelete ? m_cfg.DANGER_COLOR.lighter(120) : m_cfg.BG_COLOR);
    drawGlowEffect(p, delPath, m_deleteGlow);
    p.setPen(strokePen);
    p.drawPath(delPath);

    p.setPen(m_hoverDelete ? Qt::white : m_cfg.DANGER_COLOR);
    p.drawText(m_deleteRect, Qt::AlignCenter, m_cfg.ICON_TRASH);

    // 3. Draw Center Date Info
    QPainterPath datePath;
    datePath.addRoundedRect(m_timeRect, btnSize / 2.0, btnSize / 2.0);
    p.fillPath(datePath, m_hoverTime ? QColor(80, 80, 80, 200) : m_cfg.BG_COLOR);
    drawGlowEffect(p, datePath, m_timeGlow);
    p.setPen(strokePen);
    p.drawPath(datePath);

    p.setFont(textFont);
    p.setPen(Qt::white);
    p.drawText(m_timeRect, Qt::AlignCenter, dateStr);

}
