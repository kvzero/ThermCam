#include "gallery_view.h"
#include "core/event_bus.h"
#include "ui/widgets/gallery_topbar.h"
#include "ui/widgets/scroll_indicator.h"
#include "ui/app.h"
#include "services/gallery_service.h"

#include <QPainter>
#include <QPainterPath>
#include <QEasingCurve>
#include <cmath>
#include <algorithm>

// ============================================================================
// Initialization & Lifecycle
// ============================================================================

GalleryView::GalleryView(QWidget* parent) : BaseView(parent) {
    m_topBar = new GalleryTopBar(this);
    m_scrollIndicator = new ScrollIndicator(this);
    m_viewer = new MediaViewer(this);

    m_scrollAnim = new QPropertyAnimation(this, "activeScroll", this);
    m_zoomAnim = new QPropertyAnimation(this, "zoomProgress", this);
    m_edgeScrollTimer = new QTimer(this);

    // --- Signals: TopBar ---
    connect(m_topBar, &GalleryTopBar::backRequested, this, []() {
        emit EventBus::instance().cameraRequested();
    });

    connect(m_topBar, &GalleryTopBar::selectionModeToggled, this, [this](bool active) {
        m_isSelectionMode = active;
        if (!active) m_selectedItems.clear();
        update();
    });

    connect(m_topBar, &GalleryTopBar::selectAllClicked, this, [this]() {
        const int count = GalleryService::instance().getMediaCount();
        bool isAll = (m_selectedItems.size() == count);

        m_selectedItems.clear();
        if (!isAll) {
            for (int i = 0; i < count; ++i) m_selectedItems.insert(i);
        }
        m_topBar->updateSelectionState(m_selectedItems.size(), count);
        update();
    });

    connect(m_topBar, &GalleryTopBar::deleteRequested, this, &GalleryView::requestDeleteSelected);

    // --- Signals: Internal Timers & Animators ---
    connect(m_edgeScrollTimer, &QTimer::timeout, this, &GalleryView::performEdgeAutoScroll);
    connect(m_scrollIndicator, &ScrollIndicator::opacityChanged, this, QOverload<>::of(&QWidget::update));

    connect(m_zoomAnim, &QPropertyAnimation::finished, this, [this]() {
        m_mode = (m_zoomProgress >= m_cfg.ZOOM_TRANSITION_THRESHOLD) ? GridMode::Col4 : GridMode::Col2;
        m_activeScroll = (m_mode == GridMode::Col4) ? m_transientScroll4 : m_transientScroll2;
        m_isPinching = false;
        enforceStableState();
    });

    // --- Signals: Data Engine ---
    auto& svc = GalleryService::instance();
    connect(&svc, &GalleryService::datasetChanged, this, &GalleryView::onDatasetChanged);
    connect(&svc, &GalleryService::thumbnailUpdated, this, &GalleryView::onThumbnailUpdated);

    // --- Signals: Media Viewer ---
    connect(m_viewer, &MediaViewer::requestRedraw, this, QOverload<>::of(&QWidget::update));

    connect(m_viewer, &MediaViewer::closedCompletely, this, [this]() {
        GalleryService::instance().clearViewerCache();
        m_topBar->show();
    });

    connect(m_viewer, &MediaViewer::deleteRequested, this, [this](int index) {
        if (auto* app = qobject_cast<App*>(window())) {
            app->showConfirmDialog("DELETE MEDIA?", [this, index]() {
                GalleryService::instance().deleteMedia(index);
                m_viewer->handleDeletion();
            });
        }
    });

    connect(m_viewer, &MediaViewer::dismissRequested, this, [this]() {
        QRect targetRect = getThumbnailRectAndSync(m_viewer->currentIndex());
        m_viewer->closeTo(targetRect);
    });

    /*
     * Intent: Background Tracking & Pre-warming.
     * Keeps the occluded grid aligned with the active full-screen image.
     */
    connect(m_viewer, &MediaViewer::requestSync, this, [this](int index) {
        getThumbnailRectAndSync(index);
    });
}

void GalleryView::onEnter() {
    m_scrollAnim->stop();
    m_zoomAnim->stop();
    m_edgeScrollTimer->stop();

    m_mode = GridMode::Col2;
    m_zoomProgress = 0.0;
    m_isPinching = false;
    m_isSelectionMode = false;
    m_selectedItems.clear();

    m_topBar->setSelectionMode(false);
    m_topBar->show();

    GalleryService::instance().scanDirectory();
    m_activeScroll = getMaxScroll(currentColumns());
}

void GalleryView::onExit() {
    m_scrollAnim->stop();
    m_zoomAnim->stop();
    m_edgeScrollTimer->stop();
    GalleryService::instance().clearMemory();
}

// ============================================================================
// Gesture Routing & Handling
// ============================================================================

void GalleryView::onGestureStarted() {
    m_lastGestureDx = 0;
    m_lastGestureDy = 0;
    m_swipeAxis = SwipeAxis::None;
    m_gestureStartScroll = m_activeScroll;

    m_dragAnchorIndex = -1;
    m_selectionBaseline.clear();

    if (m_scrollAnim->state() == QAbstractAnimation::Running) {
        m_scrollAnim->stop();
    }
}

void GalleryView::onGestureUpdate(const QPoint& start, int dx, int dy) {
    int deltaX = dx - m_lastGestureDx;
    int deltaY = dy - m_lastGestureDy;
    m_lastGestureDx = dx;
    m_lastGestureDy = dy;

    const QPoint currentPos = start + QPoint(dx, dy);
    if (m_viewer->currentState() != MediaViewer::Hidden) {
         m_viewer->onPanUpdate(currentPos, deltaX, deltaY);
        return;
    }

    if (m_isPinching) return;

    m_lastDragPos = currentPos;

    if (m_swipeAxis == SwipeAxis::None) {
        int deadzone = qRound(width() * m_cfg.SWIPE_DEADZONE_RATIO);
        if (std::abs(dx) > deadzone || std::abs(dy) > deadzone) {
            m_swipeAxis = (std::abs(dx) > std::abs(dy)) ? SwipeAxis::Horizontal : SwipeAxis::Vertical;
        }
    }

    bool isEdgeBackGesture = (m_swipeAxis == SwipeAxis::Horizontal &&
                              start.x() < width() * m_cfg.SWIPE_BACK_EDGE_RATIO);

    // Intent: Handle batch selection drag with auto-scrolling
    if (m_isSelectionMode && !isEdgeBackGesture) {
        if (m_dragAnchorIndex == -1) {
            m_dragAnchorIndex = getItemIndexAt(start, currentColumns(), m_activeScroll);
            m_selectionBaseline = m_selectedItems;
        }

        updateSelectionRange(currentPos);

        const int topZone = height() * m_cfg.EDGE_SCROLL_ZONE_RATIO;
        const int bottomZone = height() * (1.0 - m_cfg.EDGE_SCROLL_ZONE_RATIO);

        if (currentPos.y() < topZone) {
            m_edgeScrollSpeed = -((topZone - currentPos.y()) / static_cast<qreal>(topZone)) * m_cfg.EDGE_SCROLL_MAX_SPEED;
            if (!m_edgeScrollTimer->isActive()) m_edgeScrollTimer->start(m_cfg.EDGE_SCROLL_INTERVAL_MS);
        } else if (currentPos.y() > bottomZone) {
            m_edgeScrollSpeed = ((currentPos.y() - bottomZone) / static_cast<qreal>(height() - bottomZone)) * m_cfg.EDGE_SCROLL_MAX_SPEED;
            if (!m_edgeScrollTimer->isActive()) m_edgeScrollTimer->start(m_cfg.EDGE_SCROLL_INTERVAL_MS);
        } else {
            m_edgeScrollTimer->stop();
        }
        return;
    }

    // Intent: Standard grid panning
    if (m_swipeAxis == SwipeAxis::None || m_swipeAxis == SwipeAxis::Vertical) {
        const qreal maxScroll = getMaxScroll(currentColumns());
        qreal target = m_gestureStartScroll - dy;

        if (target < 0) {
            target *= m_cfg.OVERSCROLL_FRICTION;
        } else if (target > maxScroll) {
            target = maxScroll + ((target - maxScroll) * m_cfg.OVERSCROLL_FRICTION);
        }
        setActiveScroll(target);
    }
}

void GalleryView::onGestureFinished(const QPoint& start, int dx, int /*dy*/, float vx, float vy) {
    if (m_viewer->currentState() != MediaViewer::Hidden) {
        m_viewer->onPanFinished(vx, vy);
        return;
    }

    m_edgeScrollTimer->stop();

    if (m_isPinching) {
        qreal targetZoom = (m_zoomProgress > m_cfg.ZOOM_TRANSITION_THRESHOLD) ? 1.0 : 0.0;
        m_zoomAnim->stop();
        m_zoomAnim->setStartValue(m_zoomProgress);
        m_zoomAnim->setEndValue(targetZoom);
        m_zoomAnim->setDuration(m_cfg.SNAP_DURATION);
        m_zoomAnim->setEasingCurve(QEasingCurve::OutBack);
        m_zoomAnim->start();
        return;
    }

    // Intent: Universal swipe-to-go-back mapping
    if (m_swipeAxis == SwipeAxis::Horizontal) {
        int edgeZone = qRound(width() * m_cfg.SWIPE_BACK_EDGE_RATIO);
        if (start.x() < edgeZone && dx > width() * m_cfg.SWIPE_BACK_DIST_RATIO) {
            if (m_isSelectionMode) {
                m_isSelectionMode = false;
                m_selectedItems.clear();
                m_topBar->setSelectionMode(false);
                update();
            } else {
                emit EventBus::instance().cameraRequested();
            }
            return;
        }
    }

    if (!m_isSelectionMode) {
        enforceStableState(vy);
    }
}

void GalleryView::onPinchUpdate(const QPoint& center, float factor) {
    if (m_viewer->currentState() != MediaViewer::Hidden) {
        return;
    }

    if (!m_isPinching) {
        m_isPinching = true;
        m_edgeScrollTimer->stop();
        m_pinchCenterY = center.y();
        m_gestureStartZoom = m_zoomProgress;

        // Intent: Calculate parallel scroll offsets for perfect visual overlap during morph
        if (m_mode == GridMode::Col2) {
            m_transientScroll2 = m_activeScroll;
            m_transientScroll4 = calculateAlignedScroll(center, 2, m_activeScroll, 4);
        } else {
            m_transientScroll4 = m_activeScroll;
            m_transientScroll2 = calculateAlignedScroll(center, 4, m_activeScroll, 2);
        }
    }

    qreal deltaZoom = (m_mode == GridMode::Col2) ?
                      (1.0 - factor) * m_cfg.PINCH_SENSITIVITY :
                      (factor - 1.0) * -m_cfg.PINCH_SENSITIVITY;

    qreal targetZoom = m_gestureStartZoom + deltaZoom;

    if (targetZoom < 0.0) {
        targetZoom *= m_cfg.ZOOM_OVERSCROLL_RESISTANCE;
    } else if (targetZoom > 1.0) {
        targetZoom = 1.0 + (targetZoom - 1.0) * m_cfg.ZOOM_OVERSCROLL_RESISTANCE;
    }

    setZoomProgress(targetZoom);
}

void GalleryView::onTapDetected(const QPoint& pos) {
    if (m_viewer->currentState() != MediaViewer::Hidden) {
        m_viewer->onTap(pos);
        return;
    }

    int index = getItemIndexAt(pos, currentColumns(), m_activeScroll);
    if (index < 0) return;

    if (m_isSelectionMode) {
        toggleSelection(index);
    } else {
        m_topBar->hide();
        QRect startRect = getThumbnailRectAndSync(index);
        m_viewer->openAt(index, startRect, size());
        GalleryService::instance().preloadViewerTrio(index, size());
    }
}

void GalleryView::onLongPressDetected(const QPoint& start) {
    if (m_viewer->currentState() != MediaViewer::Hidden) return;
    if (m_isSelectionMode) return;

    emit EventBus::instance().hapticRequested(4);
    m_topBar->setSelectionMode(true);
    m_isSelectionMode = true;

    int idx = getItemIndexAt(start, currentColumns(), m_activeScroll);
    if (idx >= 0) {
        m_dragAnchorIndex = idx;
        m_selectionBaseline = m_selectedItems;
        toggleSelection(idx);
    }
}

// ============================================================================
// Business Logic & Data Sync
// ============================================================================

void GalleryView::onDatasetChanged() {
    qreal maxScroll = getMaxScroll(currentColumns());
    if (m_activeScroll > maxScroll) {
        m_activeScroll = std::max(0.0, maxScroll);
    }
    update();
}

void GalleryView::onThumbnailUpdated(int) {
    update();
}

void GalleryView::performEdgeAutoScroll() {
    const qreal currentMax = getMaxScroll(currentColumns());
    qreal target = std::clamp(m_activeScroll + m_edgeScrollSpeed, 0.0, currentMax);
    setActiveScroll(target);
    updateSelectionRange(m_lastDragPos);
}

void GalleryView::toggleSelection(int index) {
    if (m_selectedItems.contains(index)) {
        m_selectedItems.remove(index);
    } else {
        m_selectedItems.insert(index);
    }
    m_topBar->updateSelectionState(m_selectedItems.size(), GalleryService::instance().getMediaCount());
    update();
}

void GalleryView::updateSelectionRange(const QPoint& currentPos) {
    if (m_dragAnchorIndex < 0) return;

    int currentIndex = getItemIndexAt(currentPos, currentColumns(), m_activeScroll);
    if (currentIndex >= 0) {
        int minIdx = qMin(m_dragAnchorIndex, currentIndex);
        int maxIdx = qMax(m_dragAnchorIndex, currentIndex);
        bool isSelecting = !m_selectionBaseline.contains(m_dragAnchorIndex);

        m_selectedItems = m_selectionBaseline;
        for (int i = minIdx; i <= maxIdx; ++i) {
            if (isSelecting) m_selectedItems.insert(i);
            else m_selectedItems.remove(i);
        }

        m_topBar->updateSelectionState(m_selectedItems.size(), GalleryService::instance().getMediaCount());
        update();
    }
}

void GalleryView::requestDeleteSelected() {
    if (m_selectedItems.isEmpty()) return;

    if (auto* app = qobject_cast<App*>(window())) {
        app->showConfirmDialog("DELETE SELECTED?", [this]() {
            QList<int> sortedList = m_selectedItems.values();
            std::sort(sortedList.begin(), sortedList.end(), std::greater<int>());

            for (int idx : sortedList) {
                GalleryService::instance().deleteMedia(idx);
            }

            m_selectedItems.clear();
            m_isSelectionMode = false;
            m_topBar->setSelectionMode(false);

            enforceStableState();
            update();
        });
    }
}

// ============================================================================
// Math & Geometry
// ============================================================================

void GalleryView::setActiveScroll(qreal y) {
    m_activeScroll = y;
    m_scrollIndicator->updateState(m_activeScroll, getMaxScroll(currentColumns()));
    update();
}

qreal GalleryView::getMaxScroll(int cols) const {
    if (width() <= 0) return 0.0; // Prevent div by zero before layout
    int totalItems = GalleryService::instance().getMediaCount();
    if (totalItems == 0) return 0.0;

    qreal itemH = (width() / static_cast<qreal>(cols)) * m_cfg.ASPECT_RATIO;
    qreal totalH = std::ceil(totalItems / static_cast<qreal>(cols)) * itemH;
    return std::max(0.0, totalH - height());
}

int GalleryView::getItemIndexAt(const QPoint& pos, int cols, qreal scrollY) const {
    if (width() <= 0) return -1;
    int totalItems = GalleryService::instance().getMediaCount();
    if (totalItems == 0) return -1;

    qreal itemW = width() / static_cast<qreal>(cols);
    qreal itemH = itemW * m_cfg.ASPECT_RATIO;

    int col = std::clamp(static_cast<int>(pos.x() / itemW), 0, cols - 1);
    int row = static_cast<int>((pos.y() + scrollY) / itemH);
    int index = row * cols + col;

    return (index >= totalItems) ? -1 : index;
}

qreal GalleryView::calculateAlignedScroll(const QPoint& center, int fromCols, qreal fromScroll, int toCols) const {
    if (width() <= 0 || GalleryService::instance().getMediaCount() == 0) return 0.0;

    qreal fromItemW = width() / static_cast<qreal>(fromCols);
    qreal fromItemH = fromItemW * m_cfg.ASPECT_RATIO;
    qreal absYFrom = center.y() + fromScroll;

    int fromRow = static_cast<int>(absYFrom / fromItemH);
    qreal ratioY = (absYFrom - (fromRow * fromItemH)) / fromItemH;

    int firstItemInRow = fromRow * fromCols;

    qreal toItemW = width() / static_cast<qreal>(toCols);
    qreal toItemH = toItemW * m_cfg.ASPECT_RATIO;
    int toRow = firstItemInRow / toCols;

    qreal absYTo = (toRow * toItemH) + (ratioY * toItemH);
    return absYTo - center.y();
}

QRect GalleryView::getThumbnailRectAndSync(int index) {
    if (width() <= 0) return QRect();
    int cols = currentColumns();
    qreal itemW = width() / static_cast<qreal>(cols);
    qreal itemH = itemW * m_cfg.ASPECT_RATIO;

    int row = index / cols;
    int col = index % cols;

    qreal itemTop = row * itemH;
    qreal itemBottom = itemTop + itemH;

    // Intent: Ghost Sync - Silently adjust scroll bounds to keep target visible
    if (itemTop < m_activeScroll) {
        setActiveScroll(itemTop);
    } else if (itemBottom > m_activeScroll + height()) {
        setActiveScroll(itemBottom - height());
    }

    qreal screenY = itemTop - m_activeScroll;
    QRectF box(col * itemW, screenY, itemW, itemH);
    box.adjust(m_cfg.SPACING, m_cfg.SPACING, -m_cfg.SPACING, -m_cfg.SPACING);

    return box.toRect();
}

void GalleryView::enforceStableState(float vy) {
    qreal maxScroll = getMaxScroll(currentColumns());
    qreal targetScroll = m_activeScroll;

    if (std::abs(vy) > 0.5f) {
        targetScroll -= (vy * m_cfg.VELOCITY_MULTIPLIER);
    }

    bool needsSnap = false;
    if (targetScroll < 0) { targetScroll = 0; needsSnap = true; }
    else if (targetScroll > maxScroll) { targetScroll = maxScroll; needsSnap = true; }

    m_scrollAnim->stop();
    m_scrollAnim->setStartValue(m_activeScroll);
    m_scrollAnim->setEndValue(targetScroll);

    if (needsSnap && std::abs(vy) < 0.5f) {
        m_scrollAnim->setDuration(m_cfg.SNAP_DURATION);
        m_scrollAnim->setEasingCurve(QEasingCurve::OutBack);
    } else {
        m_scrollAnim->setDuration(m_cfg.MOMENTUM_DURATION);
        m_scrollAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_scrollAnim->start();
}

// ============================================================================
// Virtual Rendering Engine
// ============================================================================

void GalleryView::resizeEvent(QResizeEvent* event) {
    BaseView::resizeEvent(event);
    m_topBar->setGeometry(0, 0, width(), qRound(height() * m_cfg.TOPBAR_HEIGHT_RATIO));
}

void GalleryView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), m_cfg.COLOR_BG);
    p.setRenderHint(QPainter::Antialiasing);

    // Intent: Occlusion Culling. Skip entirely if Viewer dominates the screen.
    if (!m_viewer->isFullyOpaque()) {
        if (GalleryService::instance().getMediaCount() == 0) {
            drawEmptyState(p);
        } else {
            if (!m_isPinching && m_zoomProgress == 0.0) {
                drawGridLayer(p, 2, m_activeScroll);
            } else if (!m_isPinching && m_zoomProgress == 1.0) {
                drawGridLayer(p, 4, m_activeScroll);
            } else {
                drawTransitionLayers(p);
            }

            if (m_scrollIndicator) {
                m_scrollIndicator->paint(p, rect());
            }
        }
    }

    // Layer 1: Single Media Viewer (Top-most)
    m_viewer->paint(p, rect());
}

void GalleryView::drawEmptyState(QPainter& p) {
    p.setPen(Qt::gray);
    QFont f = p.font();
    f.setPixelSize(m_cfg.EMPTY_STATE_FONT_SIZE);
    p.setFont(f);
    p.drawText(rect(), Qt::AlignCenter, "NO MEDIA FOUND");
}

void GalleryView::drawTransitionLayers(QPainter& p) {
    const qreal originY = m_pinchCenterY;

    // Layer A: 4-Column (Fading out / scaling up)
    p.save();
    qreal scale4 = 2.0 - 1.0 * m_zoomProgress;
    p.translate(width() / 2.0, originY);
    p.scale(scale4, scale4);
    p.translate(-width() / 2.0, -originY);
    p.setOpacity(qBound(0.0, m_zoomProgress, 1.0));
    drawGridLayer(p, 4, m_transientScroll4);
    p.restore();

    // Layer B: 2-Column (Fading in / scaling down)
    p.save();
    qreal scale2 = 1.0 - 0.5 * m_zoomProgress;
    p.translate(width() / 2.0, originY);
    p.scale(scale2, scale2);
    p.translate(-width() / 2.0, -originY);
    p.setOpacity(qBound(0.0, 1.0 - m_zoomProgress, 1.0));
    drawGridLayer(p, 2, m_transientScroll2);
    p.restore();
}

void GalleryView::drawGridLayer(QPainter& p, int cols, qreal scrollY) {
    if (width() <= 0) return;
    int totalItems = GalleryService::instance().getMediaCount();

    qreal itemW = width() / static_cast<qreal>(cols);
    qreal itemH = itemW * m_cfg.ASPECT_RATIO;

    int firstRow = std::max(0, static_cast<int>(scrollY / itemH));
    int lastRow = static_cast<int>((scrollY + height()) / itemH) + 1;

    int startIndex = firstRow * cols;
    int endIndex = std::min(totalItems - 1, (lastRow + 1) * cols);

    for (int i = startIndex; i <= endIndex; ++i) {
        QRectF box((i % cols) * itemW, (i / cols) * itemH - scrollY, itemW, itemH);
        box.adjust(m_cfg.SPACING, m_cfg.SPACING, -m_cfg.SPACING, -m_cfg.SPACING);

        MediaFileInfo info = GalleryService::instance().getMediaInfo(i);
        QSize targetSize(qRound(box.width()), qRound(box.height()));
        QImage thumb = GalleryService::instance().requestImage(i, targetSize);

        qreal cornerRadius = box.width() * m_cfg.THUMBNAIL_CORNER_RATIO;

        if (!thumb.isNull()) {
            QPainterPath clipPath;
            clipPath.addRoundedRect(box, cornerRadius, cornerRadius);
            p.save();
            p.setClipPath(clipPath);
            p.drawImage(box, thumb);
            p.restore();
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(m_cfg.COLOR_SKELETON);
            p.drawRoundedRect(box, cornerRadius, cornerRadius);
        }

        if (info.type == CaptureMode::Video) {
            drawVideoBadge(p, box, info.durationStr);
        }

        if (m_isSelectionMode) {
            drawSelectionBadge(p, box, m_selectedItems.contains(i));
        }
    }
}

void GalleryView::drawSelectionBadge(QPainter& p, const QRectF& rect, bool isSelected) {
    p.save();

    qreal iconSize = rect.height() * m_cfg.SELECTION_ICON_SIZE_RATIO;
    qreal margin = rect.width() * m_cfg.SELECTION_ICON_MARGIN_RATIO;
    QRectF iconRect(rect.right() - iconSize - margin, rect.top() + margin, iconSize, iconSize);

    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(iconSize));
    p.setFont(iconFont);

    if (isSelected) {
        p.setPen(m_cfg.CHECK_GREEN);
        p.drawText(iconRect, Qt::AlignCenter, m_cfg.ICON_CHECK_FILL);
    } else {
        p.setPen(Qt::white);
        p.drawText(iconRect, Qt::AlignCenter, m_cfg.ICON_CHECK_EMPTY);
    }

    p.restore();
}

void GalleryView::drawVideoBadge(QPainter& p, const QRectF& rect, const QString& duration) {
    p.save();

    qreal barH = rect.height() * m_cfg.VIDEO_BADGE_HEIGHT_RATIO;
    QRectF bottomBar(rect.x(), rect.bottom() - barH, rect.width(), barH);

    QPainterPath clipPath;
    qreal cornerRadius = rect.width() * m_cfg.THUMBNAIL_CORNER_RATIO;
    clipPath.addRoundedRect(rect, cornerRadius, cornerRadius);
    p.setClipPath(clipPath);

    QLinearGradient grad(bottomBar.topLeft(), bottomBar.bottomLeft());
    grad.setColorAt(0.0, Qt::transparent);
    grad.setColorAt(1.0, m_cfg.VIDEO_OVERLAY_BG);
    p.fillRect(bottomBar, grad);

    qreal hPadding = rect.width() * m_cfg.SELECTION_ICON_MARGIN_RATIO;
    qreal fontSize = barH * 0.6;

    QFont iconFont("tabler-icons");
    iconFont.setPixelSize(qRound(fontSize));
    p.setFont(iconFont);
    p.setPen(Qt::white);

    QRectF iconRect(rect.x() + hPadding, bottomBar.y(), fontSize * 1.5, bottomBar.height());
    p.drawText(iconRect, Qt::AlignLeft | Qt::AlignVCenter, m_cfg.ICON_VIDEO);

    QFont textFont("Roboto");
    textFont.setPixelSize(qRound(fontSize * 0.85));
    textFont.setBold(true);
    p.setFont(textFont);

    qreal availableTextWidth = rect.width() - iconRect.width() - (hPadding * 2.0);
    QRectF textRect(iconRect.right(), bottomBar.y(), availableTextWidth, bottomBar.height());
    p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, duration);

    p.restore();
}
