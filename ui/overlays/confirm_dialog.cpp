#include "confirm_dialog.h"
#include "core/event_bus.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEasingCurve>
#include <QDebug>

// ============================================================
// DialogButton Implementation (Logic & Hitbox)
// ============================================================

DialogButton::DialogButton(const QString& text, bool isDestructive, QWidget* parent)
    : QWidget(parent), m_text(text), m_isDestructive(isDestructive)
{
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", true);
}

void DialogButton::mousePressEvent(QMouseEvent* event) {
    m_isPressed = true;
    // Notify parent immediately before any other logic to ensure visual sync
    if (auto* dialog = qobject_cast<ConfirmDialog*>(parentWidget())) {
        dialog->setInteractionActive(true, mapToParent(event->pos()));
    }
    update();
}

void DialogButton::mouseReleaseEvent(QMouseEvent* event) {
    m_isPressed = false;
    update();

    if (rect().contains(event->pos())) {
        emit clicked();
    }
}

bool DialogButton::handleInteractionUpdate(QPoint localPos) {
    bool inside = rect().contains(localPos);
    if (inside) {
        if (auto* dialog = qobject_cast<ConfirmDialog*>(parentWidget())) {
            dialog->setInteractionActive(true, mapToParent(localPos));
        }
    }
    return inside; // If false, control flows back to ConfirmDialog::handleInteractionUpdate
}

void DialogButton::paintEvent(QPaintEvent*) {
    // Intentional empty: Visuals are managed by ConfirmDialog to sync transformations
}

// ============================================================
// ConfirmDialog Implementation (Master Renderer)
// ============================================================

ConfirmDialog::ConfirmDialog(QWidget* parent) : QWidget(parent) {
    hide();
    setProperty("isInteractable", true);
    setProperty("modalOverlay", true);
    setProperty("allowSlideTrigger", true);

    m_btnOk = new DialogButton("CONFIRM", true, this);
    m_btnCancel = new DialogButton("CANCEL", false, this);

    connect(m_btnOk, &DialogButton::clicked, this, [this](){
        if(m_confirmCallback) m_confirmCallback();
        dismiss();
    });
    connect(m_btnCancel, &DialogButton::clicked, this, &ConfirmDialog::dismiss);

    m_popAnim = new QPropertyAnimation(this, "animProgress", this);
    m_popAnim->setDuration(m_cfg.DURATION_POP_MS);

    m_touchAnim = new QPropertyAnimation(this, "touchProgress", this);
    m_touchAnim->setDuration(m_cfg.DURATION_TOUCH_MS);
    m_touchAnim->setEasingCurve(QEasingCurve::OutCubic);
}


void ConfirmDialog::mousePressEvent(QMouseEvent* event) {
    // Explicit intent: If the box is hit, activate visual feedback immediately
    if (m_boxRect.contains(event->pos())) {
        setInteractionActive(true, event->pos());
    }
    QWidget::mousePressEvent(event);
}

void ConfirmDialog::mouseReleaseEvent(QMouseEvent* event) {
    // Only dismiss if the release happens on the dark mask area (Explicit Tap)
    if (!m_boxRect.contains(event->pos())) {
        dismiss();
    }
    QWidget::mouseReleaseEvent(event);
}

void ConfirmDialog::showMessage(const QString& title, std::function<void()> onConfirm) {
    m_title = title;
    m_confirmCallback = onConfirm;

    // 1. Reset interaction state to prevent "dirty" re-entry visuals
    m_touchAnim->stop();
    m_touchProgress = 0.0;
    m_isBoxPressed = false;
    // Clean up transition states
    m_popAnim->stop();
    disconnect(m_popAnim, &QPropertyAnimation::finished, this, &QWidget::hide);

    raise();
    show();

    m_popAnim->setEasingCurve(QEasingCurve::OutBack);
    m_popAnim->setStartValue(m_animProgress);
    m_popAnim->setEndValue(1.0);
    m_popAnim->start();
}

void ConfirmDialog::dismiss() {
    // 1. Prevent multiple dismissal calls or logic re-entry
    m_confirmCallback = nullptr;

    m_popAnim->stop();

    // 2. Transition to "Vanishing" state
    m_popAnim->setDuration(m_cfg.DURATION_EXIT_MS);
    m_popAnim->setEasingCurve(QEasingCurve::InQuad);

    // 3. Ensure continuity: Start from current progress (e.g., if user clicks mid-pop)
    m_popAnim->setStartValue(m_animProgress);
    m_popAnim->setEndValue(0.0);

    // 4. Final cleanup
    connect(m_popAnim, &QPropertyAnimation::finished, this, &QWidget::hide, Qt::UniqueConnection);
    m_popAnim->start();
}

void ConfirmDialog::setInteractionActive(bool active, const QPoint& pos) {
    m_glowPos = pos;

    if (active) {
        if (!m_isBoxPressed) {
            m_isBoxPressed = true;
            m_touchAnim->stop();
            m_touchAnim->setEndValue(1.0);
            m_touchAnim->start();
        }
        update(); // Force repaint for glow movement during active interaction
    } else if (m_isBoxPressed) {
        m_isBoxPressed = false;
        m_touchAnim->stop();
        m_touchAnim->setEndValue(0.0);
        m_touchAnim->start();
    }
}

bool ConfirmDialog::handleInteractionUpdate(QPoint localPos) {
    m_glowPos = localPos;

    bool overCancel = m_btnCancel->geometry().contains(localPos);
    bool overOk = m_btnOk->geometry().contains(localPos);

    if (overCancel || overOk) {
        setInteractionActive(true, localPos);
        return false; // Hand over to the button
    }

    bool insideBox = m_boxRect.contains(localPos);
    setInteractionActive(insideBox, localPos);

    // Always return true to maintain ownership of the swipe session
    // This allows visual feedback (like glow fading) even outside the box.
    return true;
}

void ConfirmDialog::finalizeGesture(int) {
    setInteractionActive(false);
}

void ConfirmDialog::resizeEvent(QResizeEvent* event) {
    const int w = event->size().width();
    const int h = event->size().height();

    int boxW = qRound(w * m_cfg.BOX_W_RATIO);
    int boxH = qRound(h * m_cfg.BOX_H_RATIO);
    m_boxRect = QRect((w - boxW) / 2, (h - boxH) / 2, boxW, boxH);

    int btnW = qRound(boxW * m_cfg.BTN_W_RATIO);
    int btnH = qRound(boxH * m_cfg.BTN_H_RATIO);
    int hMargin = (boxW - (btnW * 2)) / 3;
    int bMargin = btnH * 0.6;
    int btnY = m_boxRect.bottom() - btnH - bMargin;

    m_btnCancel->setGeometry(m_boxRect.left() + hMargin, btnY, btnW, btnH);
    m_btnOk->setGeometry(m_boxRect.right() - hMargin - btnW, btnY, btnW, btnH);
}

void ConfirmDialog::drawButtonSkin(QPainter& p, DialogButton* btn) {
    p.save();
    p.translate(btn->pos());

    QRect r = btn->rect();
    QColor color = btn->isDestructive() ? QColor(190, 30, 30) : QColor(60, 60, 60);
    if (btn->isPressed()) color = color.lighter(130);

    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);

    p.setPen(Qt::white);
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(r.height() * 0.42);
    p.setFont(font);
    p.drawText(r, Qt::AlignCenter, btn->text());

    p.restore();
}

void ConfirmDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Layer 1: Global Dimming (Mask)
    QColor mask = m_cfg.MASK_COLOR;
    mask.setAlpha(qRound(mask.alpha() * m_animProgress));
    p.fillRect(rect(), mask);

    if (m_animProgress < 0.01) return;

    // Layer 2: Combined Space Transformation (Pop + Touch Interaction)
    qreal popScale = m_cfg.SCALE_POP_START + (1.0 - m_cfg.SCALE_POP_START) * m_animProgress;
    qreal touchScale = 1.0 + (m_cfg.SCALE_TOUCH_MAX - 1.0) * m_touchProgress;
    qreal totalScale = popScale * touchScale;

    p.save();
    p.setOpacity(m_animProgress);

    // Scale entire coordinate system around the box center
    p.translate(m_boxRect.center());
    p.scale(totalScale, totalScale);
    p.translate(-m_boxRect.center());

    // Layer 3: Box Background
    QPainterPath boxPath;
    boxPath.addRoundedRect(m_boxRect, m_cfg.BOX_CORNER_RADIUS, m_cfg.BOX_CORNER_RADIUS);

    QLinearGradient bg(m_boxRect.topLeft(), m_boxRect.bottomRight());
    bg.setColorAt(0, m_cfg.BOX_BG_START);
    bg.setColorAt(1, m_cfg.BOX_BG_END);
    p.fillPath(boxPath, bg);

    p.setPen(QPen(m_cfg.BOX_STROKE, 1.2));
    p.drawPath(boxPath);

    // Layer 4: Interactive Glow (Clipped to box)
    if (m_touchProgress > 0.01) {
        p.save();
        p.setClipPath(boxPath);
        p.setOpacity(m_touchProgress);
        QRadialGradient glow(m_glowPos, m_boxRect.width() * 0.7);
        glow.setColorAt(0.0, m_cfg.GLOW_COLOR);
        glow.setColorAt(1.0, Qt::transparent);
        p.fillRect(m_boxRect, glow);
        p.restore();
    }

    // Layer 5: Buttons (Rendered in current scaled context)
    drawButtonSkin(p, m_btnCancel);
    drawButtonSkin(p, m_btnOk);

    // Layer 6: Title
    p.setPen(Qt::white);
    QFont font = p.font();
    font.setPixelSize(m_boxRect.height() * 0.11);
    font.setBold(true);
    p.setFont(font);

    QRect textArea = m_boxRect.adjusted(30, 30, -30, 0);
    textArea.setHeight(m_boxRect.height() * 0.55);
    p.drawText(textArea, Qt::AlignCenter | Qt::TextWordWrap, m_title);

    p.restore();
}
