#include "modal_dialog.h"

#include "core/event_bus.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QEasingCurve>
#include <QStringList>

namespace {
constexpr qreal kTextModalLineGapRatio = 0.20; // Extra gap between wrapped lines.

QStringList splitModalTextLines(const QString& text) {
    return text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
}
} // namespace

ModalBase::ModalBase(QWidget* parent) : QWidget(parent) {
    hide();

    m_popAnim = new QPropertyAnimation(this, "animProgress", this);
    m_popAnim->setDuration(m_cfg.DURATION_POP_MS);
    connect(m_popAnim, &QPropertyAnimation::finished, this, &ModalBase::onPopAnimFinished);

    m_touchAnim = new QPropertyAnimation(this, "touchProgress", this);
    m_touchAnim->setDuration(m_cfg.DURATION_TOUCH_MS);
    m_touchAnim->setEasingCurve(QEasingCurve::InOutQuad);
}

void ModalBase::present(const ModalSpec& spec) {
    m_spec = spec;

    clearPressState();
    m_touchAnim->stop();
    m_touchProgress = 0.0;

    m_isDismissing = false;
    m_popAnim->stop();

    relayout();
    raise();
    show();

    m_popAnim->setDuration(m_cfg.DURATION_POP_MS);
    m_popAnim->setEasingCurve(QEasingCurve::OutBack);
    m_popAnim->setStartValue(m_animProgress);
    m_popAnim->setEndValue(1.0);
    m_popAnim->start();
}

void ModalBase::dismiss() {
    if (m_isDismissing) return;
    m_isDismissing = true;
    m_popAnim->stop();

    m_popAnim->setDuration(m_cfg.DURATION_EXIT_MS);
    m_popAnim->setEasingCurve(QEasingCurve::InQuad);
    m_popAnim->setStartValue(m_animProgress);
    m_popAnim->setEndValue(0.0);
    m_popAnim->start();
}

bool ModalBase::onPrimaryAction() {
    return true;
}

bool ModalBase::onSecondaryAction() {
    return true;
}

bool ModalBase::contentPress(const QPoint& /*contentPos*/) {
    return false;
}

bool ModalBase::contentMove(const QPoint& /*contentPos*/) {
    return false;
}

bool ModalBase::contentRelease(const QPoint& /*contentPos*/) {
    return false;
}

void ModalBase::contentCancel() {}

void ModalBase::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    beginPress(event->pos());
    event->accept();
}

void ModalBase::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    updatePress(event->pos());
    event->accept();
}

void ModalBase::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    updatePress(event->pos());
    endPress(true);
    event->accept();
}

void ModalBase::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor mask = m_cfg.MASK_COLOR;
    mask.setAlpha(qRound(mask.alpha() * m_animProgress));
    p.fillRect(rect(), mask);

    if (m_animProgress < 0.01 || m_panelRect.isEmpty()) return;

    const qreal popScale = m_cfg.SCALE_POP_START + (1.0 - m_cfg.SCALE_POP_START) * m_animProgress;
    const qreal touchScale = 1.0 + (m_cfg.SCALE_TOUCH_MAX - 1.0) * m_touchProgress;
    const qreal totalScale = popScale * touchScale;

    p.save();
    p.setOpacity(m_animProgress);
    p.translate(m_panelRect.center());
    p.scale(totalScale, totalScale);
    p.translate(-m_panelRect.center());

    QPainterPath panelPath;
    panelPath.addRoundedRect(m_panelRect, m_cfg.BOX_CORNER_RADIUS, m_cfg.BOX_CORNER_RADIUS);

    QLinearGradient bg(m_panelRect.topLeft(), m_panelRect.bottomRight());
    bg.setColorAt(0.0, m_cfg.BOX_BG_START);
    bg.setColorAt(1.0, m_cfg.BOX_BG_END);
    p.fillPath(panelPath, bg);
    p.setPen(QPen(m_cfg.BOX_STROKE, 1.2));
    p.drawPath(panelPath);

    if (m_touchProgress > 0.01) {
        p.save();
        p.setClipPath(panelPath);
        p.setOpacity(m_touchProgress);
        QRadialGradient glow(m_glowPos, m_panelRect.width() * 0.7);
        glow.setColorAt(0.0, m_cfg.GLOW_COLOR);
        glow.setColorAt(1.0, Qt::transparent);
        p.fillRect(m_panelRect, glow);
        p.restore();
    }

    paintContent(p, m_contentRect);

    const bool secondaryPressed = (m_currentTarget == PressTarget::SecondaryButton);
    const bool primaryPressed = (m_currentTarget == PressTarget::PrimaryButton);

    auto drawButton = [&](const QRect& rect, const QString& text, const QColor& base, bool pressed) {
        QColor fill = pressed ? base.lighter(130) : base;
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);

        QFont font("Roboto");
        font.setBold(true);
        font.setPixelSize(qRound(rect.height() * 0.42));
        p.setFont(font);
        p.setPen(m_cfg.BTN_TEXT);
        p.drawText(rect, Qt::AlignCenter, text);
    };

    drawButton(m_secondaryRect, m_spec.secondaryText, m_cfg.BTN_NEUTRAL, secondaryPressed);
    drawButton(m_primaryRect, m_spec.primaryText, primaryButtonColor(), primaryPressed);

    p.restore();
}

void ModalBase::resizeEvent(QResizeEvent* /*event*/) {
    relayout();
}

void ModalBase::relayout() {
    const int W = width();
    const int H = height();

    const int maxW = qRound(W * m_cfg.MAX_W_RATIO);
    const int maxH = qRound(H * m_cfg.MAX_H_RATIO);
    const int baseW = qRound(W * m_cfg.BASE_W_RATIO);
    const int baseH = qRound(H * m_cfg.BASE_H_RATIO);

    const int minContentW = qMax(50, baseW - (m_cfg.CONTENT_PAD_X * 2));
    const int minContentH = qMax(50, qRound(baseH * m_cfg.CONTENT_H_RATIO));
    const QSize maxContent(qMax(50, maxW - (m_cfg.CONTENT_PAD_X * 2)),
                           qMax(50, qRound(maxH * m_cfg.CONTENT_H_RATIO)));

    ContentLayout hint = contentLayoutHint(maxContent, QSize(W, H));
    qreal contentRatio = qBound(0.35, hint.heightRatio, 0.78);

    QSize contentSize = hint.preferred.expandedTo(QSize(minContentW, minContentH));
    contentSize = contentSize.boundedTo(maxContent);

    int panelW = qMax(baseW, contentSize.width() + (m_cfg.CONTENT_PAD_X * 2));
    int panelH = qMax(baseH, qRound(contentSize.height() / contentRatio));
    panelW = qMin(panelW, maxW);
    panelH = qMin(panelH, maxH);

    m_panelRect = QRect((W - panelW) / 2, (H - panelH) / 2, panelW, panelH);
    const int btnW = qRound(panelW * m_cfg.BTN_W_RATIO);
    const int btnH = qRound(panelH * m_cfg.BTN_H_RATIO);
    const int hMargin = (panelW - (btnW * 2)) / 3;
    const int bMargin = qRound(btnH * m_cfg.BTN_BOTTOM_MARGIN_RATIO);
    const int btnY = m_panelRect.bottom() - btnH - bMargin + 1;

    m_secondaryRect = QRect(m_panelRect.left() + hMargin, btnY, btnW, btnH);
    m_primaryRect = QRect(m_panelRect.right() - hMargin - btnW + 1, btnY, btnW, btnH);

    const int desiredContentH = qRound(panelH * contentRatio);
    const int contentAreaTop = m_panelRect.top();
    const int contentAreaBottom = btnY - 1;
    const int contentAreaH = qMax(1, contentAreaBottom - contentAreaTop + 1);
    const int contentH = qMin(desiredContentH, contentAreaH);
    const int contentTop = contentAreaTop + ((contentAreaH - contentH) / 2);

    m_contentRect = QRect(m_panelRect.left() + m_cfg.CONTENT_PAD_X,
                          contentTop,
                          panelW - (m_cfg.CONTENT_PAD_X * 2),
                          contentH);
}

ModalBase::PressTarget ModalBase::zoneAt(const QPoint& pos) const {
    if (m_secondaryRect.contains(pos)) return PressTarget::SecondaryButton;
    if (m_primaryRect.contains(pos)) return PressTarget::PrimaryButton;
    if (m_panelRect.contains(pos)) return PressTarget::Panel;
    return PressTarget::None;
}

void ModalBase::beginPress(const QPoint& localPos) {
    m_pressStartPos = localPos;
    m_lastPos = localPos;
    m_glowPos = localPos;

    const PressTarget pressed = zoneAt(localPos);
    if (pressed == PressTarget::Panel && m_contentRect.contains(localPos)) {
        const QPoint contentPos = localPos - m_contentRect.topLeft();
        if (contentPress(contentPos)) {
            m_pressStartTarget = PressTarget::Content;
            m_currentTarget = PressTarget::Content;
            setInteractionActive(m_panelRect.contains(localPos), localPos);
            update();
            return;
        }
    }

    m_pressStartTarget = pressed;
    m_currentTarget = pressed;
    setInteractionActive(m_panelRect.contains(localPos), localPos);
    update();
}

void ModalBase::updatePress(const QPoint& localPos) {
    m_lastPos = localPos;
    m_glowPos = localPos;

    if (m_pressStartTarget == PressTarget::Content) {
        contentMove(localPos - m_contentRect.topLeft());
        update();
        return;
    }

    m_currentTarget = zoneAt(localPos);
    setInteractionActive(m_panelRect.contains(localPos), localPos);
    update();
}

void ModalBase::endPress(bool allowAction) {
    const PressTarget startTarget = m_pressStartTarget;
    const PressTarget releaseZone = zoneAt(m_lastPos);
    const QPoint pressDelta = m_lastPos - m_pressStartPos;
    const bool isMaskTap =
        std::abs(pressDelta.x()) <= m_cfg.MASK_TAP_MAX_DISTANCE_PX &&
        std::abs(pressDelta.y()) <= m_cfg.MASK_TAP_MAX_DISTANCE_PX;

    if (startTarget == PressTarget::Content) {
        if (allowAction) {
            contentRelease(m_lastPos - m_contentRect.topLeft());
        } else {
            contentCancel();
        }
        clearPressState();
        return;
    }

    clearPressState();
    if (!allowAction) return;

    if (startTarget == PressTarget::PrimaryButton &&
        releaseZone == PressTarget::PrimaryButton) {
        tryPrimaryAction();
        return;
    }

    if (startTarget == PressTarget::SecondaryButton &&
        releaseZone == PressTarget::SecondaryButton) {
        trySecondaryAction();
        return;
    }

    if (startTarget == PressTarget::None &&
        releaseZone == PressTarget::None &&
        isMaskTap &&
        m_spec.dismissOnMaskTap) {
        trySecondaryAction();
    }
}

void ModalBase::clearPressState() {
    m_pressStartTarget = PressTarget::None;
    m_currentTarget = PressTarget::None;
    m_pressStartPos = QPoint();
    setInteractionActive(false);
    update();
}

void ModalBase::setInteractionActive(bool active, const QPoint& localPos) {
    m_glowPos = localPos;

    if (active) {
        if (!m_isPanelPressed) {
            m_isPanelPressed = true;
            m_touchAnim->stop();
            m_touchAnim->setStartValue(m_touchProgress);
            m_touchAnim->setEndValue(1.0);
            m_touchAnim->start();
        }
    } else if (m_isPanelPressed) {
        m_isPanelPressed = false;
        m_touchAnim->stop();
        m_touchAnim->setStartValue(m_touchProgress);
        m_touchAnim->setEndValue(0.0);
        m_touchAnim->start();
    }
}

bool ModalBase::tryPrimaryAction() {
    if (!onPrimaryAction()) return false;

    if (m_spec.level == ModalLevel::Critical) {
        emit EventBus::instance().hapticRequested(4);
    }

    if (m_spec.onPrimaryAction) m_spec.onPrimaryAction();
    dismiss();
    return true;
}

bool ModalBase::trySecondaryAction() {
    if (!onSecondaryAction()) return false;

    if (m_spec.onSecondaryAction) m_spec.onSecondaryAction();
    dismiss();
    return true;
}

QColor ModalBase::primaryButtonColor() const {
    return (m_spec.level == ModalLevel::Critical) ? m_cfg.BTN_CRITICAL : m_cfg.BTN_NEUTRAL;
}

void ModalBase::onPopAnimFinished() {
    if (!m_isDismissing) return;
    m_isDismissing = false;
    hide();
}

/* --- Text Modal Dialog --- */

TextModal::TextModal(QWidget* parent) : ModalBase(parent) {}

void TextModal::setMessage(const QString& message) {
    if (m_message == message) return;
    m_message = message;
    update();
}

ModalBase::ContentLayout TextModal::contentLayoutHint(const QSize& maxContentSize,
                                                      const QSize& viewportSize) const {
    const int fontPx = qMax(14, qRound(viewportSize.height() * 0.0605));

    QFont font("Roboto");
    font.setBold(true);
    font.setPixelSize(fontPx);

    QFontMetrics fm(font);
    const QRect wrapped = fm.boundingRect(QRect(0, 0, maxContentSize.width(), 10000),
                                          Qt::AlignCenter | Qt::TextWordWrap,
                                          m_message);

    ContentLayout out;
    out.heightRatio = 0.55;
    out.preferred = QSize(qBound(80, wrapped.width() + 8, maxContentSize.width()),
                          qBound(40, wrapped.height() + 4, maxContentSize.height()));
    return out;
}

void TextModal::paintContent(QPainter& p, const QRect& contentRect) {
    const int panelH = qRound(contentRect.height() / 0.55);
    const int fontPx = qMax(14, qRound(panelH * 0.11));

    QFont font("Roboto");
    font.setBold(true);
    font.setPixelSize(fontPx);
    p.setFont(font);
    p.setPen(Qt::white);

    const QStringList logicalLines = splitModalTextLines(m_message);
    if (logicalLines.size() <= 1) {
        p.drawText(contentRect, Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }

    QFontMetrics fm(font);
    const int maxW = contentRect.width();

    QVector<int> paragraphHeights;
    paragraphHeights.reserve(logicalLines.size());
    int textHNoGap = 0;
    for (int i = 0; i < logicalLines.size(); ++i) {
        const QString& textLine = logicalLines[i];
        const QRect wrapped = fm.boundingRect(QRect(0, 0, maxW, 10000),
                                              Qt::AlignHCenter | Qt::TextWordWrap,
                                              textLine);
        const int paraH = qMax(fm.height(), wrapped.height());
        paragraphHeights.push_back(paraH);
        textHNoGap += paraH;
    }

    const int lineCount = logicalLines.size();
    const int desiredGap = qRound(fm.height() * kTextModalLineGapRatio);
    const int maxGapBudget = qMax(0, contentRect.height() - textHNoGap);
    const int lineGap = (lineCount > 1)
                            ? qMin(desiredGap, maxGapBudget / (lineCount - 1))
                            : 0;
    const int totalH = textHNoGap + (lineCount - 1) * lineGap;

    int y = contentRect.top() + (contentRect.height() - totalH) / 2;
    for (int i = 0; i < lineCount; ++i) {
        const int paraH = paragraphHeights[i];
        if (!logicalLines[i].isEmpty()) {
            p.drawText(QRect(contentRect.left(), y, maxW, paraH),
                       Qt::AlignHCenter | Qt::TextWordWrap,
                       logicalLines[i]);
        }
        y += paraH + lineGap;
    }
}
