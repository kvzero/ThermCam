#include "bubble_dialog.h"

#include "ui/interaction_arbiter.h"

#include <QFont>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QEasingCurve>

#include <algorithm>
#include <cmath>

namespace {
QRect mapRectFromGlobal(QWidget* w, const QRect& globalRect) {
    if (!w || !globalRect.isValid()) return QRect();
    const QPoint tl = w->mapFromGlobal(globalRect.topLeft());
    const QPoint br = w->mapFromGlobal(globalRect.bottomRight());
    return QRect(tl, br).normalized();
}
} // namespace

BubbleBase::BubbleBase(QWidget* parent) : QWidget(parent) {
    hide();
    setAttribute(Qt::WA_TranslucentBackground, true);
    setProperty("isInteractable", true);
    setProperty("allowSlideTrigger", false);

    m_popAnim = new QPropertyAnimation(this, "animProgress", this);
    m_popAnim->setDuration(m_cfg.DURATION_POP_MS);
    connect(m_popAnim, &QPropertyAnimation::finished, this, &BubbleBase::onPopAnimFinished);

    m_touchAnim = new QPropertyAnimation(this, "touchProgress", this);
    m_touchAnim->setDuration(m_cfg.DURATION_TOUCH_MS);
    m_touchAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void BubbleBase::present(const BubbleAnchorContext& anchor) {
    InteractionArbiter::instance().cancelTouchSession();
    m_anchor = anchor;

    m_releasedByOutsidePan = false;
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

    onBubbleShown();
}

void BubbleBase::dismiss() {
    if (!isVisible()) return;
    if (m_isDismissing) return;
    m_isDismissing = true;
    m_popAnim->stop();

    m_popAnim->setDuration(m_cfg.DURATION_EXIT_MS);
    m_popAnim->setEasingCurve(QEasingCurve::InQuad);
    m_popAnim->setStartValue(m_animProgress);
    m_popAnim->setEndValue(0.0);
    m_popAnim->start();
}

void BubbleBase::dismissImmediately() {
    m_isDismissing = false;
    m_popAnim->stop();
    m_touchAnim->stop();
    m_animProgress = 0.0;
    m_touchProgress = 0.0;
    clearPressState();
    hide();
    emit bubbleDismissed();
    onBubbleDismissed();
}

bool BubbleBase::handleInteractionUpdate(QPoint localPos) {
    return updatePress(localPos);
}

void BubbleBase::finalizeGesture(int /*dy*/) {
    endPress(true);
    m_swallowNextRelease = true;
}

void BubbleBase::cancelGesture() {
    endPress(false);
}

bool BubbleBase::contentPress(const QPoint& /*contentPos*/) {
    return false;
}

bool BubbleBase::contentMove(const QPoint& /*contentPos*/) {
    return false;
}

bool BubbleBase::contentRelease(const QPoint& /*contentPos*/) {
    return false;
}

void BubbleBase::contentCancel() {}

void BubbleBase::paintEvent(QPaintEvent* /*event*/) {
    if (m_animProgress < 0.01 || m_panelRect.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal popScale = m_cfg.SCALE_POP_START + (1.0 - m_cfg.SCALE_POP_START) * m_animProgress;
    const qreal touchScale = 1.0 + (m_cfg.SCALE_TOUCH_MAX - 1.0) * m_touchProgress;
    const qreal totalScale = popScale * touchScale;

    p.save();
    p.setOpacity(m_animProgress);
    p.translate(m_scaleOrigin);
    p.scale(totalScale, totalScale);
    p.translate(-m_scaleOrigin);

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
        QRadialGradient glow(m_glowPos, m_panelRect.width() * 0.75);
        glow.setColorAt(0.0, m_cfg.GLOW_COLOR);
        glow.setColorAt(1.0, Qt::transparent);
        p.fillRect(m_panelRect, glow);
        p.restore();
    }

    paintContent(p, m_contentRect);
    p.restore();
}

void BubbleBase::resizeEvent(QResizeEvent* /*event*/) {
    relayout();
}

void BubbleBase::mousePressEvent(QMouseEvent* event) {
    beginPress(event->pos());
    QWidget::mousePressEvent(event);
}

void BubbleBase::mouseReleaseEvent(QMouseEvent* event) {
    m_lastPos = event->pos();
    if (m_swallowNextRelease) {
        m_swallowNextRelease = false;
        QWidget::mouseReleaseEvent(event);
        return;
    }

    endPress(true);
    QWidget::mouseReleaseEvent(event);
}

void BubbleBase::beginPress(const QPoint& localPos) {
    m_swallowNextRelease = false;
    m_releasedByOutsidePan = false;
    m_pressStartPos = localPos;
    m_lastPos = localPos;
    m_glowPos = localPos;

    if (!m_panelRect.contains(localPos)) {
        m_pressedTarget = PressTarget::Outside;
        setInteractionActive(false, localPos);
        update();
        return;
    }

    if (m_contentRect.contains(localPos)) {
        const QPoint contentPos = localPos - m_contentRect.topLeft();
        if (contentPress(contentPos)) {
            m_pressedTarget = PressTarget::Content;
            setInteractionActive(true, localPos);
            update();
            return;
        }
    }

    m_pressedTarget = PressTarget::Bubble;
    setInteractionActive(true, localPos);
    update();
}

bool BubbleBase::updatePress(const QPoint& localPos) {
    m_lastPos = localPos;
    m_glowPos = localPos;

    if (m_pressedTarget == PressTarget::Content) {
        contentMove(localPos - m_contentRect.topLeft());
        setInteractionActive(m_panelRect.contains(localPos), localPos);
        update();
        return true;
    }

    if (m_pressedTarget == PressTarget::Outside) {
        if (shouldReleaseOwnershipForOutsidePan(localPos)) {
            m_releasedByOutsidePan = true;
            clearPressState();
            dismiss();
            return false;
        }
        update();
        return true;
    }

    if (m_pressedTarget == PressTarget::Bubble) {
        setInteractionActive(m_panelRect.contains(localPos), localPos);
        update();
        return true;
    }

    return false;
}

void BubbleBase::endPress(bool allowAction) {
    const PressTarget pressedTarget = m_pressedTarget;
    const QPoint contentPos = m_lastPos - m_contentRect.topLeft();
    const bool inContent = m_contentRect.contains(m_lastPos);

    clearPressState();
    if (!allowAction) {
        if (pressedTarget == PressTarget::Content) contentCancel();
        m_releasedByOutsidePan = false;
        return;
    }

    if (pressedTarget == PressTarget::Content) {
        if (inContent) {
            contentRelease(contentPos);
        } else {
            contentCancel();
        }
        m_releasedByOutsidePan = false;
        return;
    }

    if (pressedTarget == PressTarget::Outside && !m_releasedByOutsidePan) {
        dismiss();
    }
    m_releasedByOutsidePan = false;
}

void BubbleBase::clearPressState() {
    m_pressedTarget = PressTarget::None;
    setInteractionActive(false);
    update();
}

void BubbleBase::setInteractionActive(bool active, const QPoint& localPos) {
    m_glowPos = localPos;
    if (active) {
        if (!m_isPanelPressed) {
            m_isPanelPressed = true;
            m_touchAnim->stop();
            m_touchAnim->setEndValue(1.0);
            m_touchAnim->start();
        }
    } else if (m_isPanelPressed) {
        m_isPanelPressed = false;
        m_touchAnim->stop();
        m_touchAnim->setEndValue(0.0);
        m_touchAnim->start();
    }
}

bool BubbleBase::shouldReleaseOwnershipForOutsidePan(const QPoint& localPos) const {
    const int dx = localPos.x() - m_pressStartPos.x();
    const int dy = localPos.y() - m_pressStartPos.y();
    return (std::abs(dx) > m_cfg.OUTSIDE_PAN_THRESHOLD_PX ||
            std::abs(dy) > m_cfg.OUTSIDE_PAN_THRESHOLD_PX);
}

void BubbleBase::relayout() {
    const int W = width();
    const int H = height();
    if (W <= 0 || H <= 0) return;

    QRect triggerRect = mapRectFromGlobal(this, m_anchor.triggerRectGlobal);
    if (!triggerRect.isValid()) {
        QPoint pressLocal = mapFromGlobal(m_anchor.pressPosGlobal);
        triggerRect = QRect(pressLocal.x() - 1, pressLocal.y() - 1, 3, 3);
    }
    triggerRect = triggerRect.intersected(rect());
    if (!triggerRect.isValid()) {
        triggerRect = QRect(W / 2 - 1, H / 2 - 1, 3, 3);
    }

    QRect submenuRect = mapRectFromGlobal(this, m_anchor.submenuRectGlobal);
    if (!submenuRect.isValid()) submenuRect = rect();
    submenuRect = submenuRect.intersected(rect());
    if (!submenuRect.isValid()) submenuRect = rect();

    int topBound = submenuRect.top();
    int bottomBound = submenuRect.bottom();
    if (m_anchor.submenuContentTopGlobalY >= 0) {
        topBound = mapFromGlobal(QPoint(0, m_anchor.submenuContentTopGlobalY)).y();
    }
    if (m_anchor.submenuContentBottomGlobalY >= 0) {
        bottomBound = mapFromGlobal(QPoint(0, m_anchor.submenuContentBottomGlobalY)).y();
    }
    topBound = qBound(0, topBound, H - 1);
    bottomBound = qBound(topBound, bottomBound, H - 1);

    const int minPanelW = qRound(W * m_cfg.PANEL_W_MIN_RATIO);
    const int maxPanelW = qRound(W * m_cfg.PANEL_W_MAX_RATIO);
    const int preferredPanelW = qRound(submenuRect.width() * m_cfg.PANEL_TO_SUBMENU_W_RATIO);
    const int panelW = qBound(minPanelW, preferredPanelW, maxPanelW);

    const int padX = qMax(4, qRound(panelW * m_cfg.CONTENT_PAD_X_RATIO));
    const int rowRef = qMax(1, m_anchor.referenceRowHeightPx);
    const int padYByRow = qMax(4, qRound(rowRef * m_cfg.CONTENT_PAD_Y_ROW_RATIO));
    const int padYByViewport = qMax(3, qRound(H * m_cfg.CONTENT_PAD_Y_MIN_RATIO));
    const int padY = qMax(padYByRow, padYByViewport);

    const int maxPanelHByScreen = qRound(H * m_cfg.PANEL_H_MAX_RATIO);
    int spaceBelow = qMax(0, bottomBound - triggerRect.bottom());
    int spaceAbove = qMax(0, triggerRect.top() - topBound);
    int maxAvailableH = qMax(spaceAbove, spaceBelow);
    maxAvailableH = qMin(maxAvailableH, maxPanelHByScreen);

    const QSize maxContentSize(qMax(1, panelW - (padX * 2)),
                               qMax(1, maxAvailableH - (padY * 2)));

    ContentLayout layout = contentLayoutHint(maxContentSize, QSize(W, H));
    QSize contentSize = layout.preferred.expandedTo(QSize(1, 1)).boundedTo(maxContentSize);
    int desiredPanelH = contentSize.height() + (padY * 2);
    desiredPanelH = qMin(desiredPanelH, maxPanelHByScreen);

    int panelH = desiredPanelH;
    if (desiredPanelH <= spaceBelow) {
        m_direction = ExpandDirection::Down;
    } else if (desiredPanelH <= spaceAbove) {
        m_direction = ExpandDirection::Up;
    } else if (spaceBelow >= spaceAbove) {
        m_direction = ExpandDirection::Down;
        panelH = qMax(1, spaceBelow);
    } else {
        m_direction = ExpandDirection::Up;
        panelH = qMax(1, spaceAbove);
    }

    panelH = qMax(1, qMin(panelH, maxPanelHByScreen));

    int panelX = triggerRect.center().x() - (panelW / 2);
    int xMin = submenuRect.left();
    int xMax = submenuRect.right() - panelW + 1;
    if (xMax < xMin) {
        xMin = 0;
        xMax = W - panelW;
    }
    panelX = qBound(xMin, panelX, xMax);
    panelX = qBound(0, panelX, qMax(0, W - panelW));

    int panelY = 0;
    if (m_direction == ExpandDirection::Down) {
        panelY = triggerRect.bottom() + 1;
        const int maxY = bottomBound - panelH + 1;
        panelY = qMin(panelY, maxY);
        panelY = qMax(panelY, topBound);
    } else {
        const int panelBottom = triggerRect.top() - 1;
        panelY = panelBottom - panelH + 1;
        panelY = qMax(panelY, topBound);
        if (panelY + panelH - 1 > bottomBound) {
            panelY = bottomBound - panelH + 1;
        }
    }
    panelY = qBound(0, panelY, qMax(0, H - panelH));

    m_panelRect = QRect(panelX, panelY, panelW, panelH);
    m_contentRect = QRect(m_panelRect.left() + padX,
                          m_panelRect.top() + padY,
                          qMax(1, panelW - (padX * 2)),
                          qMax(1, panelH - (padY * 2)));

    m_scaleOrigin = (m_direction == ExpandDirection::Down)
                        ? QPointF(m_panelRect.center().x(), m_panelRect.top())
                        : QPointF(m_panelRect.center().x(), m_panelRect.bottom() + 1);
}

void BubbleBase::onPopAnimFinished() {
    if (!m_isDismissing) return;
    m_isDismissing = false;
    clearPressState();
    hide();
    emit bubbleDismissed();
    onBubbleDismissed();
}

// ============================================================
// RadioListBubble
// ============================================================

RadioListBubble::RadioListBubble(QWidget* parent) : BubbleBase(parent) {
    m_scrollAnim = new QPropertyAnimation(this, "scrollOffset", this);
    m_scrollAnim->setDuration(m_cfg.SCROLL_SETTLE_MS);
    m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void RadioListBubble::present(const Spec& spec, const BubbleAnchorContext& anchor) {
    m_spec = spec;
    if (m_spec.items.isEmpty()) {
        m_selectedIndex = -1;
    } else {
        if (m_spec.selectedIndex < 0 || m_spec.selectedIndex >= m_spec.items.size()) {
            m_selectedIndex = -1;
        } else {
            m_selectedIndex = m_spec.selectedIndex;
        }
    }
    m_pressedIndex = -1;
    m_dragScrolling = false;
    m_scrollStartOffset = 0.0;
    m_scrollOffset = 0.0;
    m_scrollAnim->stop();

    BubbleBase::present(anchor);
}

void RadioListBubble::setScrollOffset(qreal value) {
    m_scrollOffset = value;
    update();
}

BubbleBase::ContentLayout RadioListBubble::contentLayoutHint(const QSize& maxContentSize,
                                                             const QSize& /*viewportSize*/) const {
    ContentLayout out;
    const int rowsH = qRound(totalRowsHeight());
    const int minH = qMax(rowHeightPx(), qMin(rowHeightPx() * 2, maxContentSize.height()));
    const int preferredH = qBound(minH, rowsH, maxContentSize.height());
    out.preferred = QSize(maxContentSize.width(), qMax(1, preferredH));
    return out;
}

void RadioListBubble::paintContent(QPainter& p, const QRect& contentRect) {
    p.save();
    p.setClipRect(contentRect);

    const int rowH = rowHeightPx();
    const int sidePad = qRound(contentRect.width() * m_cfg.ROW_SIDE_PAD_RATIO);
    const int textLeft = contentRect.left() + sidePad;
    const int iconRight = contentRect.right() - sidePad;
    const int pressInset = qMax(2, qRound(rowH * m_cfg.PRESS_INSET_RATIO));
    const qreal panelR = bubbleCornerRadius();

    for (int i = 0; i < m_spec.items.size(); ++i) {
        const int rowTop = contentRect.top() + (i * rowH) - qRound(m_scrollOffset);
        const QRect rowRect(contentRect.left(), rowTop, contentRect.width(), rowH);
        if (rowRect.bottom() < contentRect.top() || rowRect.top() > contentRect.bottom()) continue;

        if (i == m_pressedIndex) {
            QRect pressRect = rowRect.adjusted(pressInset, 0, -pressInset, 0);
            if (!pressRect.isValid()) {
                pressRect = rowRect.adjusted(1, 1, -1, -1);
            }

            const qreal byOuter = panelR * m_cfg.PRESS_RADIUS_OUTER_RATIO;
            const qreal byRow = pressRect.height() * m_cfg.PRESS_RADIUS_ROW_CAP_RATIO;
            qreal pressR = byOuter;
            if (byRow >= m_cfg.PRESS_RADIUS_MIN_PX) {
                pressR = qBound(m_cfg.PRESS_RADIUS_MIN_PX, pressR, byRow);
            } else {
                pressR = qMax(2.0, byRow);
            }

            p.setPen(Qt::NoPen);
            p.setBrush(m_cfg.PRESS_OVERLAY);
            p.drawRoundedRect(pressRect, pressR, pressR);
        }

        QFont textFont("Roboto");
        textFont.setBold(true);
        textFont.setPixelSize(qRound(rowH * m_cfg.ROW_TEXT_SIZE_RATIO));
        p.setFont(textFont);
        p.setPen(m_cfg.TEXT_COLOR);
        p.drawText(QRect(textLeft, rowRect.top(), qMax(1, iconRight - textLeft), rowRect.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_spec.items[i].text);

        if (i == m_selectedIndex) {
            QFont iconFont("tabler-icons");
            iconFont.setPixelSize(qRound(rowH * 0.44));
            p.setFont(iconFont);
            p.setPen(m_cfg.CHECK_COLOR);
            const QRect iconRect(iconRight - rowH, rowRect.top(), rowH, rowRect.height());
            p.drawText(iconRect, Qt::AlignVCenter | Qt::AlignRight, QString(QChar(0xea5e)));
        }
    }

    p.restore();
}

bool RadioListBubble::contentPress(const QPoint& contentPos) {
    if (m_spec.items.isEmpty()) return false;
    m_scrollAnim->stop();
    m_dragScrolling = false;
    m_contentPressPos = contentPos;
    m_contentLastPos = contentPos;
    m_scrollStartOffset = m_scrollOffset;
    m_pressedIndex = rowIndexAt(contentPos);
    update();
    return true;
}

bool RadioListBubble::contentMove(const QPoint& contentPos) {
    m_contentLastPos = contentPos;
    const int dy = contentPos.y() - m_contentPressPos.y();
    const bool canScroll = (maxScroll() > 0.5);

    if (canScroll && (m_dragScrolling || std::abs(dy) > m_cfg.SCROLL_DRAG_THRESHOLD_PX)) {
        m_dragScrolling = true;
        m_pressedIndex = -1;
        const qreal candidate = m_scrollStartOffset - dy;
        setScrollOffset(applyOverscroll(candidate));
        return true;
    }

    if (!QRect(QPoint(0, 0), contentRect().size()).contains(contentPos)) {
        m_pressedIndex = -1;
    } else {
        m_pressedIndex = rowIndexAt(contentPos);
    }
    update();
    return true;
}

bool RadioListBubble::contentRelease(const QPoint& contentPos) {
    const bool wasScrolling = m_dragScrolling;
    m_dragScrolling = false;

    if (wasScrolling) {
        settleScrollIfNeeded();
        m_pressedIndex = -1;
        update();
        return true;
    }

    const int releaseIndex = rowIndexAt(contentPos);
    const bool shouldSelect = (m_pressedIndex >= 0 && m_pressedIndex == releaseIndex);
    m_pressedIndex = -1;

    if (shouldSelect) {
        m_selectedIndex = releaseIndex;
        if (m_spec.onSelected) {
            const QString id = m_spec.items[m_selectedIndex].id;
            m_spec.onSelected(m_selectedIndex, id);
        }
        if (m_spec.dismissOnSelection) {
            dismiss();
        }
    }

    update();
    return shouldSelect;
}

void RadioListBubble::contentCancel() {
    m_pressedIndex = -1;
    m_dragScrolling = false;
    settleScrollIfNeeded();
    update();
}

void RadioListBubble::onBubbleDismissed() {
    if (m_spec.onDismissed) m_spec.onDismissed();
}

int RadioListBubble::rowHeightPx() const {
    return qMax(28, qRound(referenceRowHeight() * m_cfg.ROW_H_REF_RATIO));
}

int RadioListBubble::rowIndexAt(const QPoint& contentPos) const {
    if (m_spec.items.isEmpty()) return -1;
    if (contentPos.y() < 0 || contentPos.y() >= contentRect().height()) return -1;

    const qreal logicalY = contentPos.y() + m_scrollOffset;
    const int idx = static_cast<int>(std::floor(logicalY / qMax(1, rowHeightPx())));
    if (idx < 0 || idx >= m_spec.items.size()) return -1;
    return idx;
}

qreal RadioListBubble::totalRowsHeight() const {
    return static_cast<qreal>(m_spec.items.size()) * rowHeightPx();
}

qreal RadioListBubble::maxScroll() const {
    return qMax(0.0, totalRowsHeight() - contentRect().height());
}

void RadioListBubble::settleScrollIfNeeded() {
    qreal target = m_scrollOffset;
    const qreal maxV = maxScroll();
    if (m_scrollOffset < 0.0) {
        target = 0.0;
    } else if (m_scrollOffset > maxV) {
        target = maxV;
    } else {
        return;
    }

    m_scrollAnim->stop();
    m_scrollAnim->setStartValue(m_scrollOffset);
    m_scrollAnim->setEndValue(target);
    m_scrollAnim->start();
}

qreal RadioListBubble::applyOverscroll(qreal candidate) const {
    const qreal maxV = maxScroll();
    constexpr qreal kFriction = 0.35;
    if (candidate < 0.0) return candidate * kFriction;
    if (candidate > maxV) return maxV + (candidate - maxV) * kFriction;
    return candidate;
}

// ============================================================
// SliderBubble
// ============================================================

SliderBubble::SliderBubble(QWidget* parent) : BubbleBase(parent) {}

void SliderBubble::present(const Spec& spec, const BubbleAnchorContext& anchor) {
    m_spec = spec;
    m_value = normalizeValue(m_spec.value);
    m_sliderActive = false;
    BubbleBase::present(anchor);
}

BubbleBase::ContentLayout SliderBubble::contentLayoutHint(const QSize& maxContentSize,
                                                          const QSize& /*viewportSize*/) const {
    ContentLayout out;
    const int preferredH = qMax(36, qRound(referenceRowHeight() * 0.92));
    out.preferred = QSize(maxContentSize.width(), qMin(preferredH, maxContentSize.height()));
    return out;
}

void SliderBubble::paintContent(QPainter& p, const QRect& contentRect) {
    recalcGeometry(contentRect);

    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(m_iconRect.height() * m_cfg.FONT_RATIO));
    p.setFont(iconFont);
    p.setPen(m_cfg.ICON_COLOR);
    const QString icon = m_spec.iconGlyph.isEmpty() ? QString(QChar(0xeb20)) : m_spec.iconGlyph;
    p.drawText(m_iconRect, Qt::AlignCenter, icon);

    const qreal trackR = m_trackRect.height() / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(m_cfg.TRACK_BG);
    p.drawRoundedRect(m_trackRect, trackR, trackR);

    const qreal ratio = valueToRatio(m_value);
    QRect activeRect = m_trackRect;
    activeRect.setWidth(qRound(m_trackRect.width() * ratio));
    activeRect.setWidth(qMax(activeRect.width(), m_trackRect.height()));
    p.setBrush(m_spec.accentColor);
    p.drawRoundedRect(activeRect, trackR, trackR);

    const int handleD = qRound(m_trackRect.height() * 2.1);
    const int handleX = m_trackRect.left() + qRound(m_trackRect.width() * ratio) - (handleD / 2);
    const int handleY = m_trackRect.center().y() - (handleD / 2);
    QRect handleRect(handleX, handleY, handleD, handleD);
    p.setBrush(m_cfg.HANDLE_COLOR);
    p.drawEllipse(handleRect);
}

bool SliderBubble::contentPress(const QPoint& contentPos) {
    recalcGeometry(contentRect());
    m_sliderActive = isTrackInteractive(contentPos);
    if (!m_sliderActive) return true;
    updateValueByPointer(contentPos, true);
    return true;
}

bool SliderBubble::contentMove(const QPoint& contentPos) {
    recalcGeometry(contentRect());
    if (!m_sliderActive) return true;
    updateValueByPointer(contentPos, true);
    return true;
}

bool SliderBubble::contentRelease(const QPoint& contentPos) {
    recalcGeometry(contentRect());
    if (!m_sliderActive) return false;

    updateValueByPointer(contentPos, true);
    if (m_spec.onValueCommitted) m_spec.onValueCommitted(m_value);

    m_sliderActive = false;
    if (m_spec.dismissOnCommit) dismiss();
    return true;
}

void SliderBubble::contentCancel() {
    m_sliderActive = false;
}

void SliderBubble::onBubbleDismissed() {
    if (m_spec.onDismissed) m_spec.onDismissed();
}

int SliderBubble::normalizeValue(int value) const {
    if (m_spec.maxValue <= m_spec.minValue) return m_spec.minValue;

    const int clamped = qBound(m_spec.minValue, value, m_spec.maxValue);
    const int step = qMax(1, m_spec.step);
    const int span = clamped - m_spec.minValue;
    const int snapped = m_spec.minValue + qRound(static_cast<qreal>(span) / step) * step;
    return qBound(m_spec.minValue, snapped, m_spec.maxValue);
}

int SliderBubble::valueFromTrackX(int x) const {
    if (m_trackRect.width() <= 0 || m_spec.maxValue <= m_spec.minValue) {
        return m_spec.minValue;
    }

    int left = m_trackRect.left();
    int right = m_trackRect.right();

    int edgeSnap = qMax(m_cfg.TRACK_EDGE_SNAP_MIN_PX,
                        qRound(m_trackRect.width() * m_cfg.TRACK_EDGE_SNAP_RATIO));
    edgeSnap = qBound(0, edgeSnap, qMax(0, (m_trackRect.width() - 1) / 2));
    if (edgeSnap > 0) {
        if (x <= left + edgeSnap) return m_spec.minValue;
        if (x >= right - edgeSnap) return m_spec.maxValue;
        left += edgeSnap;
        right -= edgeSnap;
    }

    const int denom = qMax(1, right - left);
    const qreal ratio = qBound(0.0, static_cast<qreal>(x - left) / denom, 1.0);
    const qreal raw = m_spec.minValue + ratio * (m_spec.maxValue - m_spec.minValue);
    return normalizeValue(qRound(raw));
}

qreal SliderBubble::valueToRatio(int value) const {
    if (m_spec.maxValue <= m_spec.minValue) return 0.0;
    return static_cast<qreal>(value - m_spec.minValue) /
           static_cast<qreal>(m_spec.maxValue - m_spec.minValue);
}

void SliderBubble::recalcGeometry(const QRect& contentRect) {
    const int h = contentRect.height();
    const int w = contentRect.width();

    const int iconSlotW = qRound(w * m_cfg.ICON_SLOT_RATIO);
    const int iconD = qRound(h * m_cfg.ICON_SIZE_RATIO);
    const int iconX = contentRect.left() + qRound((iconSlotW - iconD) * 0.5);
    const int iconY = contentRect.center().y() - (iconD / 2);
    m_iconRect = QRect(iconX, iconY, iconD, iconD);

    const int trackLeft = contentRect.left() + iconSlotW + qRound(w * m_cfg.TRACK_GAP_RATIO);
    const int trackRight = contentRect.right() - qRound(w * m_cfg.TRACK_END_PAD_RATIO);
    const int trackW = qMax(20, trackRight - trackLeft + 1);
    const int trackH = qMax(4, qRound(h * m_cfg.TRACK_H_RATIO));
    const int trackY = contentRect.center().y() - (trackH / 2);
    m_trackRect = QRect(trackLeft, trackY, trackW, trackH);

    const int hitPadY = qMax(6, qRound(h * m_cfg.TRACK_HIT_PAD_RATIO));
    const int hitPadX = qMax(6, qRound(w * m_cfg.TRACK_HIT_PAD_X_RATIO));
    m_trackHitRect = m_trackRect.adjusted(-hitPadX, -hitPadY, hitPadX, hitPadY);
}

bool SliderBubble::isTrackInteractive(const QPoint& contentPos) const {
    const QRect contentLocal(QPoint(0, 0), contentRect().size());
    if (!contentLocal.contains(contentPos)) return false;
    const QPoint widgetPos = contentPos + contentRect().topLeft();
    return m_trackHitRect.contains(widgetPos);
}

void SliderBubble::updateValueByPointer(const QPoint& contentPos, bool notifyChanging) {
    const int widgetX = contentPos.x() + contentRect().left();
    const int newValue = valueFromTrackX(widgetX);
    if (newValue == m_value) return;
    m_value = newValue;
    if (notifyChanging && m_spec.onValueChanging) {
        m_spec.onValueChanging(m_value);
    }
    update();
}
