#include "gallery_topbar.h"
#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QTimer>
#include <QFontMetricsF>
#include <cmath>

GalleryTopBar::GalleryTopBar(QWidget* parent) : QWidget(parent) {
    setProperty("isInteractable", true);

    auto setupAnim = [this](QPropertyAnimation** anim, const char* prop, int dur, QEasingCurve::Type curve) {
        *anim = new QPropertyAnimation(this, prop, this);
        (*anim)->setDuration(dur);
        (*anim)->setEasingCurve(curve);
    };

    setupAnim(&m_morphAnim, "morphProgress", 450, QEasingCurve::OutBack); // Bouncy metaball
    setupAnim(&m_widthAnim, "trashExtraWidth", 250, QEasingCurve::OutQuad);
    setupAnim(&m_leftGlowAnim, "leftGlow", 150, QEasingCurve::OutQuad);
    setupAnim(&m_rightGlowAnim, "rightGlow", 150, QEasingCurve::OutQuad);
    setupAnim(&m_eruptionAnim, "eruption", 400, QEasingCurve::OutQuad); // Shockwave expansion
    setupAnim(&m_retractGlowAnim, "retractGlow", 400, QEasingCurve::OutQuad);
}

void GalleryTopBar::setSelectionMode(bool active) {
    if (m_isSelectionMode == active) return;
    m_isSelectionMode = active;

    if (!active) {
        /* Intent: Wipe data and forcefully collapse the trash pill when exiting */
        m_isAllSelected = false;
        m_selectedCount = 0;
        m_targetExtraWidth = 0.0;

        m_widthAnim->stop();
        m_widthAnim->setEndValue(0.0);
        m_widthAnim->start();

        /* Intent: Trigger the red energy retraction scanner from left to right */
        m_retractGlowAnim->stop();
        m_retractGlowAnim->setStartValue(0.0);
        m_retractGlowAnim->setEndValue(1.0);
        m_retractGlowAnim->start();
    } else {
        /* Intent: Lock the release point as the epicenter for the red energy eruption */
        m_eruptionOrigin = m_lastGlowPos;
        m_eruptionAnim->stop();
        m_eruptionAnim->setStartValue(0.0);
        m_eruptionAnim->setEndValue(1.0);
        m_eruptionAnim->start();
    }

    m_morphAnim->stop();
    m_morphAnim->setStartValue(m_morphProgress);
    m_morphAnim->setEndValue(active ? 1.0 : 0.0);
    m_morphAnim->start();
}

void GalleryTopBar::updateSelectionState(int selectedCount, int totalCount) {
    m_selectedCount = selectedCount;
    m_totalCount = totalCount;
    m_isAllSelected = (selectedCount > 0 && selectedCount == totalCount);

    qreal targetExtra = 0.0;
    if (m_selectedCount > 0) {
        /* Intent: Dynamically calculate physical expansion required for the text */
        const qreal btnH = height() * m_cfg.BTN_HEIGHT_RATIO;
        QFont f("Roboto");
        f.setPixelSize(qRound(btnH * 0.45));
        f.setBold(true);
        QFontMetricsF fm(f);

        qreal textW = fm.horizontalAdvance(QString::number(m_selectedCount));
        qreal padding = btnH * 0.5; // Left + Right padding inside the white pill
        qreal margin = btnH * 0.15; // Gap between Trash icon and white pill
        targetExtra = textW + padding + margin;
    }

    m_targetExtraWidth = targetExtra;

    if (std::abs(m_widthAnim->endValue().toReal() - targetExtra) > 1.0) {
        m_widthAnim->stop();
        m_widthAnim->setStartValue(m_trashExtraWidth);
        m_widthAnim->setEndValue(targetExtra);
        m_widthAnim->start();
    }
    update();
}

void GalleryTopBar::triggerGlowAnimation(QPropertyAnimation* anim, bool active) {
    qreal target = active ? 1.0 : 0.0;
    if (anim->endValue().toReal() != target) {
        anim->stop();
        anim->setEndValue(target);
        anim->start();
    }
}

bool GalleryTopBar::handleInteractionUpdate(QPoint localPos) {
    m_lastGlowPos = localPos;

    /* Intent: Massively artificially inflate the hitboxes for blind tactile operation */
    qreal expand = height() * 0.4;
    QRectF touchLeft = m_leftHitbox.adjusted(-expand, -expand, expand, expand);
    QRectF touchCancel = m_rightCancelHitbox.adjusted(-expand, -expand, expand, expand);
    QRectF touchTrash = m_rightTrashHitbox.adjusted(-expand, -expand, expand, expand);

    bool newHoverLeft = touchLeft.contains(localPos);
    bool newHoverCancel = touchCancel.contains(localPos);
    bool newHoverTrash = (m_isSelectionMode && touchTrash.contains(localPos));

    triggerGlowAnimation(m_leftGlowAnim, newHoverLeft);
    triggerGlowAnimation(m_rightGlowAnim, newHoverCancel || newHoverTrash);

    m_hoverLeft = newHoverLeft;
    m_hoverRightCancel = newHoverCancel;
    m_hoverTrash = newHoverTrash;

    update();

    /* Intent: Surrender gesture lock if sliding out of the physical button zones */
    return (m_hoverLeft || m_hoverRightCancel || m_hoverTrash);
}

void GalleryTopBar::finalizeGesture(int) {
    triggerGlowAnimation(m_leftGlowAnim, false);
    triggerGlowAnimation(m_rightGlowAnim, false);

    if (m_hoverLeft) {
        if (m_isSelectionMode) emit selectAllClicked();
        else emit backRequested();
    }
    else if (m_hoverTrash) {
        /* Intent: Delay execution by 100ms to bypass InteractionArbiter's synthetic Tap Collision */
        QTimer::singleShot(100, this, &GalleryTopBar::deleteRequested);
    }
    else if (m_hoverRightCancel) {
        setSelectionMode(!m_isSelectionMode);
        emit selectionModeToggled(m_isSelectionMode);
    }

    m_hoverLeft = m_hoverRightCancel = m_hoverTrash = false;
    update();
}

void GalleryTopBar::mousePressEvent(QMouseEvent* event) {
    handleInteractionUpdate(event->pos());
    QWidget::mousePressEvent(event);
}

void GalleryTopBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int H = height();
    drawLeftAction(p, H);
    drawRightAction(p, H);
}

void GalleryTopBar::drawLeftAction(QPainter& p, int H) {
    const qreal margin = H * m_cfg.MARGIN_RATIO;
    const qreal btnH = H * m_cfg.BTN_HEIGHT_RATIO;

    qreal currentW = btnH + (btnH * 1.4 * m_morphProgress);
    m_leftHitbox = QRectF(margin, margin, currentW, btnH);

    QPainterPath path;
    path.addRoundedRect(m_leftHitbox, btnH / 2.0, btnH / 2.0);

    // 1. Draw Base
    p.fillPath(path, m_cfg.BG_NORMAL);

    // 2. Draw Finger Tracking Glow (Clipped to pill)
    if (m_leftGlow > 0.01) {
        p.save();
        p.setClipPath(path);
        p.setOpacity(m_leftGlow);
        QRadialGradient glow(m_lastGlowPos, btnH * 1.5);
        glow.setColorAt(0.0, m_cfg.GLOW_WHITE);
        glow.setColorAt(1.0, Qt::transparent);
        p.fillRect(m_leftHitbox, glow);
        p.restore();
    }

    // 3. Draw Stroke & Content
    p.setPen(QPen(m_cfg.STROKE_COLOR, 1.2));
    p.drawPath(path);

    p.setPen(Qt::white);
    if (m_morphProgress < 0.5) {
        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(btnH * 0.5));
        p.setFont(iconFont);
        p.setOpacity(1.0 - (m_morphProgress * 2.0));
        p.drawText(m_leftHitbox, Qt::AlignCenter, m_cfg.ICON_BACK);
    } else {
        QFont textFont("Roboto");
        textFont.setPixelSize(qRound(btnH * 0.4));
        textFont.setBold(true);
        p.setFont(textFont);
        p.setOpacity((m_morphProgress - 0.5) * 2.0);
        p.drawText(m_leftHitbox, Qt::AlignCenter, m_isAllSelected ? "Clear" : "Select All");
    }
    p.setOpacity(1.0);
}

void GalleryTopBar::drawRightAction(QPainter& p, int H) {
    const qreal margin = H * m_cfg.MARGIN_RATIO;
    const qreal btnH = H * m_cfg.BTN_HEIGHT_RATIO;
    const qreal r = btnH / 2.0;

    // --- Geometric Anchors ---
    qreal normalW = btnH * 2.1;
    qreal selectW = btnH * 2.4;
    qreal pillW = normalW + ((selectW - normalW) * m_morphProgress);

    QRectF pillRect(width() - margin - pillW, margin, pillW, btnH);
    m_rightCancelHitbox = pillRect;

    QPainterPath pillPath;
    pillPath.addRoundedRect(pillRect, r, r);

    // Metaball & Trash Anchors
    QPointF cCenter(pillRect.left() + r, pillRect.center().y());
    qreal maxStretch = btnH * 1.2;
    QPointF tCenter(cCenter.x() - maxStretch * m_morphProgress, cCenter.y());

    qreal tRightEdge = tCenter.x() + r;
    qreal tTotalW = btnH + m_trashExtraWidth;
    qreal tLeftEdge = tRightEdge - tTotalW;
    m_rightTrashHitbox = QRectF(tLeftEdge, tCenter.y() - r, tTotalW, btnH);

    // ==========================================
    // Layer 1: Cancel Pill Base & Interactive Glow
    // ==========================================
    p.fillPath(pillPath, m_cfg.BG_NORMAL);

    if (m_rightGlow > 0.01) {
        p.save();
        p.setClipPath(pillPath);
        p.setOpacity(m_rightGlow);
        QRadialGradient glow(m_lastGlowPos, btnH * 1.5);
        glow.setColorAt(0.0, m_cfg.GLOW_WHITE);
        glow.setColorAt(1.0, Qt::transparent);
        p.fillRect(pillRect, glow);
        p.restore();
    }

    // ==========================================
    // Layer 2: The Red Energy Eruption (Elliptical Shockwave)
    // ==========================================
    if (m_eruption > 0.0 && m_eruption < 1.0) {
        p.save();
        p.setClipPath(pillPath);

        p.translate(m_eruptionOrigin);
        p.scale(2.5, 1.0);
        p.translate(-m_eruptionOrigin);

        qreal eruptRadius = pillRect.width() * m_eruption;
        QRadialGradient eruptGrad(m_eruptionOrigin, eruptRadius);
        eruptGrad.setColorAt(0.0, m_cfg.TRASH_RED);
        eruptGrad.setColorAt(1.0, Qt::transparent);

        p.setOpacity(1.0 - m_eruption);
        QRectF inverseRect = p.transform().inverted().mapRect(pillRect);
        p.fillRect(inverseRect, eruptGrad);
        p.restore();
    }

    // ==========================================
    // Layer 2.5: The Energy Retraction (Scanner Wave)
    // ==========================================
    if (m_retractGlow > 0.0 && m_retractGlow < 1.0) {
        p.save();
        p.setClipPath(pillPath);

        qreal scanWidth = pillRect.width() * 0.8;
        qreal scanX = pillRect.left() - scanWidth + (pillRect.width() + scanWidth * 2.0) * m_retractGlow;

        QLinearGradient scanGrad(scanX - scanWidth / 2.0, 0, scanX + scanWidth / 2.0, 0);
        scanGrad.setColorAt(0.0, Qt::transparent);
        scanGrad.setColorAt(0.5, m_cfg.TRASH_RED);
        scanGrad.setColorAt(1.0, Qt::transparent);

        p.setOpacity(1.0 - m_retractGlow);
        p.fillRect(pillRect, scanGrad);
        p.restore();
    }

    qreal trashAlpha = 1.0;
    if (m_morphProgress < 0.6) {
        trashAlpha = 0.3 + 0.7 * (m_morphProgress / 0.6);
    }

    // ==========================================
    // Layer 3: Gooey Metaball Bridge (Liquid Detachment)
    // ==========================================
    qreal distance = cCenter.x() - tCenter.x();
    qreal breakThreshold = maxStretch * 0.85;

    if (m_morphProgress > 0.0 && distance < breakThreshold) {
        p.save();
        p.setOpacity(trashAlpha);

        qreal tension = (distance / breakThreshold) * (r * 0.85);

        QPointF T_top(tCenter.x(), tCenter.y() - r);
        QPointF T_bot(tCenter.x(), tCenter.y() + r);
        QPointF C_top(cCenter.x(), cCenter.y() - r);
        QPointF C_bot(cCenter.x(), cCenter.y() + r);

        QPainterPath bridge;
        bridge.moveTo(T_top);
        bridge.quadTo(QPointF(tCenter.x() + distance / 2.0, tCenter.y() - r + tension), C_top);
        bridge.lineTo(C_bot);
        bridge.quadTo(QPointF(tCenter.x() + distance / 2.0, tCenter.y() + r - tension), T_bot);
        bridge.closeSubpath();

        QLinearGradient bridgeGrad(tCenter, cCenter);
        bridgeGrad.setColorAt(0.0, m_cfg.TRASH_RED);
        bridgeGrad.setColorAt(1.0, m_cfg.BG_NORMAL);
        p.fillPath(bridge, bridgeGrad);

        p.restore();
    }

    // ==========================================
    // Layer 4: Trash Capsule & Pop-Scale Number Badge
    // ==========================================
    if (m_morphProgress > 0.0) {
        p.save();
        p.setOpacity(trashAlpha);

        QPainterPath trashPath;
        trashPath.addRoundedRect(m_rightTrashHitbox, r, r);
        p.fillPath(trashPath, m_hoverTrash ? m_cfg.TRASH_RED.lighter(120) : m_cfg.TRASH_RED);

        QRectF iconRect(tLeftEdge, tCenter.y() - r, btnH, btnH);
        p.setPen(Qt::white);
        QFont iconFont("tabler-icons");
        iconFont.setPixelSize(qRound(btnH * 0.65));
        p.setFont(iconFont);
        p.drawText(iconRect, Qt::AlignCenter, m_cfg.ICON_TRASH);

        if (m_targetExtraWidth > 2.0) {
            qreal popScale = std::clamp(m_trashExtraWidth / m_targetExtraWidth, 0.0, 1.0);

            qreal pillPad = btnH * 0.15;
            qreal inPillH = btnH - pillPad * 2.0;
            qreal inPillW = m_targetExtraWidth - pillPad;

            QRectF inPillRect(iconRect.right(), tCenter.y() - inPillH / 2.0, inPillW, inPillH);

            p.save();
            p.translate(inPillRect.center());
            p.scale(popScale, popScale);
            p.translate(-inPillRect.center());

            QPainterPath inPillPath;
            inPillPath.addRoundedRect(inPillRect, inPillH / 2.0, inPillH / 2.0);
            p.fillPath(inPillPath, Qt::white);

            p.setPen(m_cfg.TRASH_RED);
            QFont numFont("Roboto");
            numFont.setPixelSize(qRound(inPillH * 0.7));
            numFont.setBold(true);
            p.setFont(numFont);
            p.drawText(inPillRect, Qt::AlignCenter, QString::number(m_selectedCount));

            p.restore();
        }

        p.restore();
    }

    // ==========================================
    // Layer 5: Cancel Pill Stroke & Text
    // ==========================================
    p.setPen(QPen(m_cfg.STROKE_COLOR, 1.2));
    p.drawPath(pillPath);

    p.setPen(Qt::white);
    QFont textFont("Roboto");
    textFont.setPixelSize(qRound(btnH * 0.4));
    textFont.setBold(true);
    p.setFont(textFont);

    if (m_morphProgress < 0.5) {
        p.setOpacity(1.0 - (m_morphProgress * 2.0));
        p.drawText(pillRect, Qt::AlignCenter, "SELECT");
    } else {
        p.setOpacity((m_morphProgress - 0.5) * 2.0);
        p.drawText(pillRect, Qt::AlignCenter, "CANCEL");
    }
    p.setOpacity(1.0);
}
