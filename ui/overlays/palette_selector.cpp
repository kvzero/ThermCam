#include "palette_selector.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>
#include <cmath>
#include <limits>

namespace {

/* --- Rendering Constants --- */
constexpr qreal kVisibleDeltaLimit = 2.2;
constexpr qreal kReflectionHeightRatio = 0.5;
constexpr qreal kReflectionGapRatio = 0.04;
constexpr qreal kReflectionInsetRatio = 0.02;
constexpr qreal kCardBottomInsetRatio = 0.08;

/* --- Visual Curves --- */
qreal cardScaleFromDelta(qreal absDelta) {
    return qBound(0.84, 1.0 - (absDelta * 0.12), 1.0);
}

qreal cardAlphaFromDelta(qreal absDelta) {
    return qBound(0.45, 1.0 - (absDelta * 0.30), 1.0);
}

qreal cardGlowFromDelta(qreal absDelta) {
    return qBound(0.0, 1.0 - absDelta, 1.0);
}

/* --- Carousel Geometry --- */
struct CarouselGeometry {
    QVector<qreal> centerOffsets;
    QVector<qreal> cardWidths;
    qreal focusOffset = 0.0;
};

CarouselGeometry buildCarouselGeometry(int count, qreal centerIndex, qreal baseCardW, qreal gapPx) {
    CarouselGeometry layout;
    if (count <= 0 || baseCardW <= 0.0) return layout;

    layout.centerOffsets = QVector<qreal>(count, 0.0);
    layout.cardWidths = QVector<qreal>(count, 0.0);
    for (int i = 0; i < count; ++i) {
        const qreal absDelta = std::abs(static_cast<qreal>(i) - centerIndex);
        layout.cardWidths[i] = baseCardW * cardScaleFromDelta(absDelta);
    }

    const int maxIndex = count - 1;
    const int anchor = qBound(0, static_cast<int>(std::floor(centerIndex)), maxIndex);
    layout.centerOffsets[anchor] = 0.0;

    for (int i = anchor + 1; i <= maxIndex; ++i) {
        const qreal d = ((layout.cardWidths[i - 1] + layout.cardWidths[i]) * 0.5) + gapPx;
        layout.centerOffsets[i] = layout.centerOffsets[i - 1] + d;
    }
    for (int i = anchor - 1; i >= 0; --i) {
        const qreal d = ((layout.cardWidths[i] + layout.cardWidths[i + 1]) * 0.5) + gapPx;
        layout.centerOffsets[i] = layout.centerOffsets[i + 1] - d;
    }

    if (count == 1) {
        layout.focusOffset = 0.0;
        return layout;
    }

    if (centerIndex < 0.0) {
        const qreal step = layout.centerOffsets[1] - layout.centerOffsets[0];
        layout.focusOffset = layout.centerOffsets[0] + (centerIndex * step);
        return layout;
    }

    if (centerIndex > static_cast<qreal>(maxIndex)) {
        const qreal step = layout.centerOffsets[maxIndex] - layout.centerOffsets[maxIndex - 1];
        const qreal over = centerIndex - static_cast<qreal>(maxIndex);
        layout.focusOffset = layout.centerOffsets[maxIndex] + (over * step);
        return layout;
    }

    const int left = static_cast<int>(std::floor(centerIndex));
    const int right = qMin(left + 1, maxIndex);
    const qreal t = centerIndex - static_cast<qreal>(left);
    layout.focusOffset = layout.centerOffsets[left] +
                         ((layout.centerOffsets[right] - layout.centerOffsets[left]) * t);
    return layout;
}

} // namespace

/* --- Lifecycle --- */
PaletteSelector::PaletteSelector(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    hide();

    const int paletteCount = static_cast<int>(ThermalPalette::kPaletteCount);
    m_paletteIds.reserve(paletteCount);
    for (int i = 0; i < paletteCount; ++i) {
        m_paletteIds.append(static_cast<ThermalPalette::Id>(i));
    }
    m_previewFrames.resize(paletteCount);

    m_panelAnim = new QPropertyAnimation(this, "panelProgress", this);
    m_panelAnim->setDuration(m_cfg.PANEL_ANIM_MS);
    m_panelAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_snapAnim = new QPropertyAnimation(this, "centerIndex", this);
    m_snapAnim->setDuration(m_cfg.SNAP_ANIM_MS);
    m_snapAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_panelAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_panelProgress <= 0.001) {
            finishDismiss();
        }
    });
}

/* --- Session Control --- */
void PaletteSelector::present(ThermalPalette::Id initial) {
    const int initialIndex = indexForPalette(initial);
    if (initialIndex < 0) return;

    m_snapAnim->stop();
    m_panelAnim->stop();

    m_isPresented = true;
    m_commitOnClose = false;
    m_mode = InteractionMode::None;
    m_dragAxis = DragAxis::Unknown;
    m_lastPreviewIndex = -1;
    m_centerIndex = initialIndex;
    clearPreviewFrames();

    show();
    raise();

    setPanelProgress(0.0);
    notifyPreviewSelection();

    m_panelAnim->setStartValue(panelProgress());
    m_panelAnim->setEndValue(1.0);
    m_panelAnim->start();
}

void PaletteSelector::dismiss(bool commitSelection) {
    if (!m_isPresented) return;

    m_mode = InteractionMode::None;
    m_dragAxis = DragAxis::Unknown;
    m_commitOnClose = commitSelection;

    m_snapAnim->stop();
    m_panelAnim->stop();
    m_panelAnim->setStartValue(panelProgress());
    m_panelAnim->setEndValue(0.0);
    m_panelAnim->start();
}

ThermalPalette::Id PaletteSelector::currentPalette() const {
    return paletteForIndex(nearestIndex(m_centerIndex));
}

/* --- Preview Feed --- */
void PaletteSelector::setPreviewFrame(ThermalPalette::Id id, const QImage& frame) {
    const int index = indexForPalette(id);
    if (index < 0 || index >= m_previewFrames.size()) return;

    m_previewFrames[index] = frame;
    update(previewRect().adjusted(-8, -8, 8, 8));
}

void PaletteSelector::clearPreviewFrames() {
    for (QImage& frame : m_previewFrames) {
        frame = QImage();
    }
    update();
}

QSize PaletteSelector::previewFrameSize() const {
    const QRect preview = previewRect();
    if (preview.isEmpty()) return QSize();

    const qreal spacing = itemSpacingPx();
    const int w = qRound(spacing * m_cfg.CENTER_CARD_FROM_SPACING);
    const int h = qRound(w / m_cfg.CARD_ASPECT);
    return QSize(qMax(1, w), qMax(1, h));
}

/* --- Interaction Handling --- */
void PaletteSelector::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_isPresented) {
        QWidget::mousePressEvent(event);
        return;
    }

    beginPress(event->pos());
    event->accept();
}

void PaletteSelector::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton) || !m_isPresented) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    updatePress(event->pos());
    event->accept();
}

void PaletteSelector::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_isPresented) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    updatePress(event->pos());
    endPress();
    event->accept();
}

void PaletteSelector::beginPress(const QPoint& localPos) {
    if (!m_isPresented) return;

    m_panelAnim->stop();
    m_snapAnim->stop();

    m_pressLocalPos = localPos;
    m_lastLocalPos = localPos;
    m_previousLocalPos = localPos;
    m_velocityPxPerMs = QPointF();
    m_dragTimer.restart();
    m_previousSampleMs = 0;
    m_dragStartCenterIndex = m_centerIndex;
    m_dragStartPanelProgress = m_panelProgress;
    m_dragAxis = DragAxis::Unknown;

    const QRect panel = panelRect();
    const QRect preview = previewRect();
    const bool pressInPreview = preview.contains(localPos);

    if (!panel.contains(localPos)) {
        m_mode = InteractionMode::OutsideTap;
        return;
    }

    if (pressInPreview) {
        m_mode = InteractionMode::PreviewDrag;
        return;
    }

    m_mode = InteractionMode::Passive;
}

void PaletteSelector::updatePress(const QPoint& localPos) {
    if (!m_isPresented) return;

    updateDragVelocity(localPos);
    m_lastLocalPos = localPos;
    const qreal dx = static_cast<qreal>(localPos.x() - m_pressLocalPos.x());
    const qreal dy = static_cast<qreal>(localPos.y() - m_pressLocalPos.y());

    switch (m_mode) {
    case InteractionMode::OutsideTap: {
        if (std::hypot(dx, dy) > m_cfg.DISMISS_TAP_SLOP_PX) {
            m_mode = InteractionMode::Passive;
        }
        return;
    }
    case InteractionMode::Passive: {
        if (std::abs(dy) > m_cfg.DRAG_SLOP_PX && std::abs(dy) > std::abs(dx) && dy > 0) {
            m_mode = InteractionMode::DismissDrag;
            m_dragStartPanelProgress = m_panelProgress;
        }
        return;
    }
    case InteractionMode::DismissDrag: {
        const QRect panel = panelRect();
        if (panel.height() <= 0) return;

        const qreal progress = m_dragStartPanelProgress - (dy / panel.height());
        setPanelProgress(qBound(0.0, progress, 1.0));
        return;
    }
    case InteractionMode::PreviewDrag: {
        if (m_dragAxis == DragAxis::Unknown) {
            if (std::hypot(dx, dy) < m_cfg.DRAG_SLOP_PX) {
                return;
            }

            if (std::abs(dy) > std::abs(dx)) {
                m_dragAxis = DragAxis::Vertical;
                if (dy > 0) {
                    m_mode = InteractionMode::DismissDrag;
                    m_dragStartPanelProgress = m_panelProgress;
                }
                return;
            }

            m_dragAxis = DragAxis::Horizontal;
        }

        if (m_dragAxis != DragAxis::Horizontal) {
            return;
        }

        const qreal spacing = itemSpacingPx();
        if (spacing < 1.0) return;

        const qreal candidate = m_dragStartCenterIndex - (dx / spacing);
        setCenterIndex(applyEdgeResistance(candidate));
        return;
    }
    case InteractionMode::None:
        return;
    }
}

void PaletteSelector::endPress() {
    if (!m_isPresented) return;

    const qreal dx = static_cast<qreal>(m_lastLocalPos.x() - m_pressLocalPos.x());
    const qreal dy = static_cast<qreal>(m_lastLocalPos.y() - m_pressLocalPos.y());

    switch (m_mode) {
    case InteractionMode::OutsideTap: {
        dismiss(true);
        break;
    }
    case InteractionMode::DismissDrag: {
        const QRect panel = panelRect();
        const qreal threshold = panel.height() * m_cfg.DISMISS_DRAG_THRESHOLD_RATIO;
        if (dy > threshold || m_panelProgress < 0.55) {
            dismiss(true);
        } else {
            m_panelAnim->stop();
            m_panelAnim->setStartValue(m_panelProgress);
            m_panelAnim->setEndValue(1.0);
            m_panelAnim->start();
        }
        break;
    }
    case InteractionMode::PreviewDrag: {
        const bool horizontalMoved = (m_dragAxis == DragAxis::Horizontal) &&
                                     (std::abs(dx) > m_cfg.DRAG_SLOP_PX);
        if (horizontalMoved) {
            const qreal spacing = itemSpacingPx();
            if (spacing >= 1.0) {
                constexpr qreal kInertiaHorizonMs = 80.0;
                constexpr qreal kMaxProjectedSteps = 1.0;
                const qreal projected = m_centerIndex - ((m_velocityPxPerMs.x() * kInertiaHorizonMs) / spacing);
                const qreal projectedDelta = projected - m_centerIndex;
                const qreal limitedProjected =
                    m_centerIndex + qBound(-kMaxProjectedSteps, projectedDelta, kMaxProjectedSteps);
                animateSnapToIndex(nearestIndex(limitedProjected));
            } else {
                animateSnapToNearest();
            }
        } else {
            animateSnapToIndex(tappedIndex(m_pressLocalPos));
        }
        break;
    }
    case InteractionMode::Passive:
    case InteractionMode::None:
        break;
    }

    m_mode = InteractionMode::None;
    m_dragAxis = DragAxis::Unknown;
}

void PaletteSelector::cancelPress() {
    if (!m_isPresented) return;

    if (m_mode == InteractionMode::PreviewDrag) {
        animateSnapToNearest();
    }

    if (m_mode == InteractionMode::DismissDrag) {
        m_panelAnim->stop();
        m_panelAnim->setStartValue(m_panelProgress);
        m_panelAnim->setEndValue(1.0);
        m_panelAnim->start();
    }

    m_mode = InteractionMode::None;
    m_dragAxis = DragAxis::Unknown;
}

void PaletteSelector::updateDragVelocity(const QPoint& localPos) {
    if (!m_dragTimer.isValid()) {
        m_dragTimer.start();
        m_previousSampleMs = 0;
        m_previousLocalPos = localPos;
        m_velocityPxPerMs = QPointF();
        return;
    }

    const qint64 nowMs = m_dragTimer.elapsed();
    const qint64 elapsedMs = nowMs - m_previousSampleMs;
    if (elapsedMs <= 0) {
        return;
    }

    const QPoint delta = localPos - m_previousLocalPos;
    if (delta.isNull()) {
        m_previousSampleMs = nowMs;
        return;
    }

    const QPointF instantaneous(delta.x() / static_cast<qreal>(elapsedMs),
                                 delta.y() / static_cast<qreal>(elapsedMs));
    constexpr qreal kVelocityWeight = 0.45;
    m_velocityPxPerMs = (m_velocityPxPerMs * (1.0 - kVelocityWeight)) +
                        (instantaneous * kVelocityWeight);
    m_previousLocalPos = localPos;
    m_previousSampleMs = nowMs;
}

/* --- Animated Properties --- */
void PaletteSelector::setPanelProgress(qreal value) {
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_panelProgress, value)) return;

    m_panelProgress = value;
    update();
}

void PaletteSelector::setCenterIndex(qreal value) {
    const qreal minValue = -0.60;
    const qreal maxValue = m_paletteIds.isEmpty()
                               ? 0.0
                               : static_cast<qreal>(m_paletteIds.size() - 1) + 0.60;
    value = qBound(minValue, value, maxValue);
    if (qFuzzyCompare(m_centerIndex, value)) return;

    m_centerIndex = value;
    notifyPreviewSelection();
    update(previewRect().adjusted(-24, -24, 24, 24).united(titleRect().adjusted(-8, -8, 8, 8)));
}

/* --- Rendering Helpers --- */
bool PaletteSelector::buildCarouselLayout(const QRect& preview,
                                          QVector<qreal>* outCenterOffsets,
                                          QVector<qreal>* outCardWidths,
                                          qreal* outFocusOffset) const {
    if (!outCenterOffsets || !outCardWidths || !outFocusOffset) return false;
    if (preview.isEmpty() || m_paletteIds.isEmpty()) return false;

    const qreal spacing = itemSpacingPx();
    if (spacing < 1.0) return false;

    const qreal baseCardW = spacing * m_cfg.CENTER_CARD_FROM_SPACING;
    const qreal gapPx = baseCardW * m_cfg.GAP_FROM_BASE;
    const CarouselGeometry layout =
        buildCarouselGeometry(m_paletteIds.size(), m_centerIndex, baseCardW, gapPx);
    if (layout.centerOffsets.size() != m_paletteIds.size()) return false;

    *outCenterOffsets = layout.centerOffsets;
    *outCardWidths = layout.cardWidths;
    *outFocusOffset = layout.focusOffset;
    return true;
}

void PaletteSelector::paintMaskLayer(QPainter& p, const QRect& mask) const {
    QLinearGradient gradient(mask.left(), mask.top(), mask.left(), mask.bottom());
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(m_cfg.MASK_CURVE_MID1_POS, QColor(0, 0, 0, m_cfg.MASK_CURVE_MID1_ALPHA));
    gradient.setColorAt(m_cfg.MASK_CURVE_MID2_POS, QColor(0, 0, 0, m_cfg.MASK_CURVE_MID2_ALPHA));
    gradient.setColorAt(1.0, QColor(0, 0, 0, m_cfg.MASK_CURVE_BOTTOM_ALPHA));
    p.fillRect(mask, gradient);
}

void PaletteSelector::paintTitleLayer(QPainter& p, const QRect& title) const {
    QFont titleFont("Roboto");
    titleFont.setBold(true);
    titleFont.setPixelSize(qRound(title.height() * 0.56));
    p.setFont(titleFont);

    p.setPen(QColor(0, 0, 0, 130));
    p.drawText(title.translated(0, 2), Qt::AlignCenter, displayName(currentPalette()));
    p.setPen(Qt::white);
    p.drawText(title, Qt::AlignCenter, displayName(currentPalette()));
}

void PaletteSelector::paintCardsLayer(QPainter& p,
                                      const QRect& preview,
                                      const QVector<qreal>& centerOffsets,
                                      const QVector<qreal>& cardWidths,
                                      qreal focusOffset) const {
    if (centerOffsets.size() != m_paletteIds.size() || cardWidths.size() != m_paletteIds.size()) {
        return;
    }

    const qreal centerX = preview.center().x();
    const qreal cardBottomY = preview.bottom() - (preview.height() * kCardBottomInsetRatio);

    for (int i = 0; i < m_paletteIds.size(); ++i) {
        const qreal delta = static_cast<qreal>(i) - m_centerIndex;
        const qreal absDelta = std::abs(delta);
        if (absDelta > kVisibleDeltaLimit) continue;

        const qreal alpha = cardAlphaFromDelta(absDelta);
        const qreal glow = cardGlowFromDelta(absDelta);

        const qreal cardW = cardWidths[i];
        const qreal cardH = cardW / m_cfg.CARD_ASPECT;
        const qreal cx = centerX + (centerOffsets[i] - focusOffset);
        if ((cx + cardW * 0.5) < (preview.left() - 8) || (cx - cardW * 0.5) > (preview.right() + 8)) {
            continue;
        }
        const QRectF cardRect(cx - cardW * 0.5, cardBottomY - cardH, cardW, cardH);

        const qreal radius = cardRect.height() * 0.09;
        QPainterPath clipPath;
        clipPath.addRoundedRect(cardRect, radius, radius);

        // Soft drop shadow creates separation from the mask background.
        {
            const qreal shadowOffsetX = 0.8 + (0.6 * glow);
            const qreal shadowOffsetY = 1.8 + (1.2 * glow);
            const QRectF shadowRect =
                cardRect.translated(shadowOffsetX, shadowOffsetY).adjusted(-1.2, -0.2, 2.0, 2.6);
            const QColor shadowColor(0, 0, 0, qRound(82 + (102 * glow)));
            p.setPen(Qt::NoPen);
            p.setBrush(shadowColor);
            p.drawRoundedRect(shadowRect, radius + 1.2, radius + 1.2);
        }

        const bool hasFrame = (i < m_previewFrames.size() && !m_previewFrames[i].isNull());

        p.save();
        p.setOpacity(alpha);
        p.setClipPath(clipPath);
        if (hasFrame) {
            p.drawImage(cardRect.toRect(), m_previewFrames[i]);
        } else {
            p.fillRect(cardRect, QColor(30, 30, 30, 220));
        }
        p.restore();

        if (!hasFrame) continue;

        const qreal reflectionGap = cardRect.height() * kReflectionGapRatio;
        const qreal reflectionH = cardRect.height() * kReflectionHeightRatio;
        const QRectF reflectionRect(cardRect.left() + (cardRect.width() * kReflectionInsetRatio),
                                    cardRect.bottom() + reflectionGap,
                                    cardRect.width() * (1.0 - (2.0 * kReflectionInsetRatio)),
                                    reflectionH);
        const QRectF visibleRect = reflectionRect.intersected(QRectF(preview));
        if (visibleRect.width() <= 2.0 || visibleRect.height() <= 2.0) continue;

        const qreal reflectionRadius = qMin(radius * 0.9, reflectionRect.height() * 0.45);
        QPainterPath reflectionShape;
        reflectionShape.addRoundedRect(reflectionRect, reflectionRadius, reflectionRadius);

        p.save();
        p.setClipRect(visibleRect);
        p.setClipPath(reflectionShape, Qt::IntersectClip);
        p.setOpacity(alpha * (0.54 + (0.1 * glow)));
        p.translate(0.0, reflectionRect.top() + reflectionRect.bottom());
        p.scale(1.0, -1.0);
        p.drawImage(reflectionRect.toRect(), m_previewFrames[i]);
        p.restore();

        p.save();
        p.setClipRect(visibleRect);
        p.setClipPath(reflectionShape, Qt::IntersectClip);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QLinearGradient fade(reflectionRect.left(), reflectionRect.top(),
                             reflectionRect.left(), reflectionRect.bottom());
        fade.setColorAt(0.0, QColor(0, 0, 0, qRound(190 + (30 * glow))));
        fade.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(reflectionRect, fade);
        p.restore();
    }
}

/* --- QWidget Overrides --- */
void PaletteSelector::paintEvent(QPaintEvent* /*event*/) {
    if (m_panelProgress <= 0.001) return;

    const QRect panel = panelRect();
    const QRect preview = previewRect();
    const QRect mask = maskRect();
    const QRect title = titleRect();
    if (panel.isEmpty() || preview.isEmpty() || mask.isEmpty()) return;

    QVector<qreal> centerOffsets;
    QVector<qreal> cardWidths;
    qreal focusOffset = 0.0;
    if (!buildCarouselLayout(preview, &centerOffsets, &cardWidths, &focusOffset)) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    paintMaskLayer(p, mask);
    paintTitleLayer(p, title);
    paintCardsLayer(p, preview, centerOffsets, cardWidths, focusOffset);
}

void PaletteSelector::resizeEvent(QResizeEvent* /*event*/) {
    update();
}

/* --- Geometry Helpers --- */
QRect PaletteSelector::panelRect() const {
    if (width() <= 0 || height() <= 0) return QRect();

    const int panelH = qMax(1, qRound(height() * m_cfg.PANEL_HEIGHT_RATIO));
    const int top = height() - qRound(panelH * m_panelProgress);
    return QRect(0, top, width(), panelH);
}

QRect PaletteSelector::titleRect() const {
    if (width() <= 0 || height() <= 0) return QRect();

    const int titleH = qMax(1, qRound(height() * m_cfg.TITLE_HEIGHT_RATIO));
    const int targetY = qRound(height() * m_cfg.TITLE_TOP_RATIO);
    const int hiddenY = -titleH - 2;
    const int y = qRound(hiddenY + ((targetY - hiddenY) * m_panelProgress));
    return QRect(0, y, width(), titleH);
}

QRect PaletteSelector::previewRect() const {
    const QRect panel = panelRect();
    if (panel.isEmpty()) return QRect();

    const int topInset = qRound(panel.height() * 0.03);
    return panel.adjusted(0, topInset, 0, 0);
}

QRect PaletteSelector::maskRect() const {
    const QRect preview = previewRect();
    if (preview.isEmpty()) return QRect();

    const int maskTop = preview.top() + qRound(preview.height() * m_cfg.MASK_TOP_CUTOFF_RATIO);
    return QRect(preview.left(), maskTop, preview.width(), preview.bottom() - maskTop + 1);
}

qreal PaletteSelector::itemSpacingPx() const {
    const QRect preview = previewRect();
    if (preview.isEmpty()) return 0.0;
    return preview.width() * m_cfg.SPACING_RATIO;
}

/* --- Index Helpers --- */
int PaletteSelector::tappedIndex(const QPoint& pos) const {
    const QRect preview = previewRect();
    if (preview.isEmpty() || m_paletteIds.isEmpty()) {
        return nearestIndex(m_centerIndex);
    }

    QVector<qreal> centerOffsets;
    QVector<qreal> cardWidths;
    qreal focusOffset = 0.0;
    if (!buildCarouselLayout(preview, &centerOffsets, &cardWidths, &focusOffset)) {
        return nearestIndex(m_centerIndex);
    }

    const qreal localX = static_cast<qreal>(pos.x() - preview.center().x()) + focusOffset;

    int containingIndex = -1;
    qreal containingDist = std::numeric_limits<qreal>::max();
    int nearest = 0;
    qreal nearestDist = std::numeric_limits<qreal>::max();

    for (int i = 0; i < m_paletteIds.size(); ++i) {
        const qreal dist = std::abs(localX - centerOffsets[i]);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = i;
        }

        if (dist <= (cardWidths[i] * 0.5) && dist < containingDist) {
            containingDist = dist;
            containingIndex = i;
        }
    }

    return (containingIndex >= 0) ? containingIndex : nearest;
}

int PaletteSelector::indexForPalette(ThermalPalette::Id id) const {
    const int raw = static_cast<int>(id);
    if (raw < 0 || raw >= m_paletteIds.size()) return -1;
    return raw;
}

ThermalPalette::Id PaletteSelector::paletteForIndex(int index) const {
    if (m_paletteIds.isEmpty()) return ThermalPalette::Id::Spectra;
    index = qBound(0, index, m_paletteIds.size() - 1);
    return m_paletteIds[index];
}

int PaletteSelector::nearestIndex(qreal value) const {
    if (m_paletteIds.isEmpty()) return 0;
    return qBound(0, qRound(value), m_paletteIds.size() - 1);
}

qreal PaletteSelector::applyEdgeResistance(qreal candidate) const {
    if (m_paletteIds.isEmpty()) return 0.0;

    const qreal min = 0.0;
    const qreal max = static_cast<qreal>(m_paletteIds.size() - 1);
    if (candidate < min) {
        return min + ((candidate - min) * m_cfg.EDGE_RESISTANCE);
    }
    if (candidate > max) {
        return max + ((candidate - max) * m_cfg.EDGE_RESISTANCE);
    }
    return candidate;
}

void PaletteSelector::notifyPreviewSelection() {
    const int idx = nearestIndex(m_centerIndex);
    if (idx == m_lastPreviewIndex) return;

    m_lastPreviewIndex = idx;
    emit previewSelectionChanged(paletteForIndex(idx));
}

QString PaletteSelector::displayName(ThermalPalette::Id id) const {
    switch (id) {
    case ThermalPalette::Id::WhiteHot:
        return tr("White Hot");
    case ThermalPalette::Id::BlackHot:
        return tr("Black Hot");
    case ThermalPalette::Id::Spectra:
        return tr("Spectra");
    case ThermalPalette::Id::Prism:
        return tr("Prism");
    case ThermalPalette::Id::Tyrian:
        return tr("Tyrian");
    case ThermalPalette::Id::Iron:
        return tr("Iron");
    case ThermalPalette::Id::Amber:
        return tr("Amber");
    case ThermalPalette::Id::Hi:
        return tr("High Contrast");
    case ThermalPalette::Id::Green:
        return tr("Green");
    case ThermalPalette::Id::Count:
        break;
    }
    return tr("Palette");
}

/* --- Animation Engine --- */
void PaletteSelector::animateSnapToIndex(int index) {
    index = qBound(0, index, m_paletteIds.size() - 1);

    m_snapAnim->stop();
    m_snapAnim->setStartValue(m_centerIndex);
    m_snapAnim->setEndValue(index);
    m_snapAnim->start();
}

void PaletteSelector::animateSnapToNearest() {
    animateSnapToIndex(nearestIndex(m_centerIndex));
}

void PaletteSelector::finishDismiss() {
    if (!m_isPresented) return;

    if (m_commitOnClose) {
        emit selectionCommitted(currentPalette());
    }

    m_isPresented = false;
    m_commitOnClose = false;
    m_mode = InteractionMode::None;
    m_dragAxis = DragAxis::Unknown;
    hide();
}
