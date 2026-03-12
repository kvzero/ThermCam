#ifndef GALLERY_VIEW_H
#define GALLERY_VIEW_H

#include "ui/views/base_view.h"
#include "ui/overlays/media_viewer.h"
#include <QPropertyAnimation>
#include <QSet>
#include <QTimer>

class GalleryTopBar;
class ScrollIndicator;

/**
 * @brief Layer 0 View: Core media presentation and layout engine.
 *
 * Implements a dynamic grid system with a dual-layer crossfade mechanism
 * to support seamless semantic zooming (Pinch-to-Zoom). Features a
 * high-performance virtual rendering pipeline and delegates single-item
 * interaction to MediaViewer.
 */
class GalleryView : public BaseView {
    Q_OBJECT
    Q_PROPERTY(qreal activeScroll READ activeScroll WRITE setActiveScroll)
    Q_PROPERTY(qreal zoomProgress READ zoomProgress WRITE setZoomProgress)

public:
    explicit GalleryView(QWidget* parent = nullptr);
    ~GalleryView() override = default;

    /* --- Lifecycle Hooks --- */
    void onEnter() override;
    void onExit() override;
    void handleKeyShortPress() override {}

    /* --- Semantic Gesture Dispatchers --- */
    void onGestureStarted() override;
    void onGestureUpdate(const QPoint& start, int dx, int dy) override;
    void onGestureFinished(const QPoint& start, int dx, int dy, float vx, float vy) override;
    void onPinchUpdate(const QPoint& center, float factor) override;
    void onTapDetected(const QPoint& pos) override;
    void onLongPressDetected(const QPoint& start) override;

    /* --- Animation Property Accessors --- */
    qreal activeScroll() const { return m_activeScroll; }
    void setActiveScroll(qreal y);

    qreal zoomProgress() const { return m_zoomProgress; }
    void setZoomProgress(qreal p) { m_zoomProgress = p; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onDatasetChanged();
    void onThumbnailUpdated(int index);
    void performEdgeAutoScroll();

private:
    enum class GridMode { Col2, Col4 };
    enum class SwipeAxis { None, Horizontal, Vertical };

    /** @brief Centralized Magic Numbers & Styling */
    struct Config {
        /* Layout & Aesthetics */
        const qreal ASPECT_RATIO                = 0.75;
        const qreal SPACING                     = 2.0;
        const qreal TOPBAR_HEIGHT_RATIO         = 0.23;
        const qreal THUMBNAIL_CORNER_RATIO      = 0.05;
        const qreal SELECTION_ICON_SIZE_RATIO   = 0.20;
        const qreal SELECTION_ICON_MARGIN_RATIO = 0.04;
        const qreal VIDEO_BADGE_HEIGHT_RATIO    = 0.25;
        const int   EMPTY_STATE_FONT_SIZE       = 24;

        /* Colors & Icons */
        const QColor COLOR_SKELETON             = QColor(45, 45, 45);
        const QColor COLOR_BG                   = QColor(10, 10, 10);
        const QColor VIDEO_OVERLAY_BG           = QColor(0, 0, 0, 140);
        const QColor CHECK_GREEN                = QColor(52, 199, 89);
        const QString ICON_VIDEO                = QChar(0xed22);
        const QString ICON_CHECK_EMPTY          = QChar(0xf59a);
        const QString ICON_CHECK_FILL           = QChar(0xf6df);

        /* Physics & Gestures */
        const qreal OVERSCROLL_FRICTION         = 0.35;
        const float VELOCITY_MULTIPLIER         = 180.0f;
        const float PINCH_SENSITIVITY           = 2.8f;
        const qreal ZOOM_OVERSCROLL_RESISTANCE  = 0.1;
        const qreal ZOOM_TRANSITION_THRESHOLD   = 0.5;

        const qreal SWIPE_DEADZONE_RATIO        = 0.02;
        const qreal SWIPE_BACK_EDGE_RATIO       = 0.10;
        const qreal SWIPE_BACK_DIST_RATIO       = 0.15;

        const qreal EDGE_SCROLL_ZONE_RATIO      = 0.15;
        const qreal EDGE_SCROLL_MAX_SPEED       = 15.0f;
        const int   EDGE_SCROLL_INTERVAL_MS     = 16;

        const int   MOMENTUM_DURATION           = 800;
        const int   SNAP_DURATION               = 350;
    } m_cfg;

    /* --- Core State Machine --- */
    GridMode m_mode = GridMode::Col2;
    SwipeAxis m_swipeAxis = SwipeAxis::None;
    qreal m_activeScroll = 0.0;
    qreal m_zoomProgress = 0.0;

    /* --- Pinch Engine --- */
    bool m_isPinching = false;
    qreal m_pinchCenterY = 0.0;
    qreal m_transientScroll2 = 0.0;
    qreal m_transientScroll4 = 0.0;
    qreal m_gestureStartScroll = 0.0;
    qreal m_gestureStartZoom = 0.0;

    /* --- Batch Edit Engine --- */
    bool m_isSelectionMode = false;
    QSet<int> m_selectedItems;
    QSet<int> m_selectionBaseline;
    QTimer* m_edgeScrollTimer = nullptr;
    qreal m_edgeScrollSpeed = 0.0;
    QPoint m_lastDragPos;
    int m_dragAnchorIndex = -1;

    /* --- Gesture Deltas --- */
    int m_lastGestureDx = 0;
    int m_lastGestureDy = 0;

    /* --- Sub-Components --- */
    GalleryTopBar* m_topBar = nullptr;
    ScrollIndicator* m_scrollIndicator = nullptr;
    MediaViewer* m_viewer = nullptr;
    QPropertyAnimation* m_scrollAnim = nullptr;
    QPropertyAnimation* m_zoomAnim = nullptr;

    /* --- Internal Math Helpers --- */
    int currentColumns() const { return (m_mode == GridMode::Col2) ? 2 : 4; }
    qreal getMaxScroll(int cols) const;
    int getItemIndexAt(const QPoint& pos, int cols, qreal scrollY) const;
    qreal calculateAlignedScroll(const QPoint& center, int fromCols, qreal fromScroll, int toCols) const;
    QRect getThumbnailRectAndSync(int index);

    /* --- Logic Controllers --- */
    void toggleSelection(int index);
    void updateSelectionRange(const QPoint& currentPos);
    void requestDeleteSelected();
    void enforceStableState(float vy = 0.0f);

    /* --- Render Pipelines --- */
    void drawEmptyState(QPainter& p);
    void drawTransitionLayers(QPainter& p);
    void drawGridLayer(QPainter& p, int cols, qreal scrollY);
    void drawSelectionBadge(QPainter& p, const QRectF& rect, bool isSelected);
    void drawVideoBadge(QPainter& p, const QRectF& rect, const QString& duration);
};

#endif // GALLERY_VIEW_H
