#ifndef MEDIA_VIEWER_H
#define MEDIA_VIEWER_H

#include <QObject>
#include <QPropertyAnimation>
#include <QImage>
#include <QRect>
#include <QPainter>
#include "core/types.h"
#include "ui/widgets/viewer_topbar.h"
#include "ui/widgets/video_controlbar.h"
#include "media/video_player.h"

/**
 * @brief Zero-Widget Orchestrator for Full-Screen Media Playback.
 *
 * Intent: Acts as the central nervous system for the viewer mode.
 * It does NOT handle low-level rendering of controls or decoding logic.
 * Instead, it arbitrates gestures between sub-components (TopBar, VideoBar, ImagePlane)
 * and synchronizes the VideoPlayer engine with the UI representation.
 */
class MediaViewer : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal slideOffset READ slideOffset WRITE setSlideOffset)
    Q_PROPERTY(qreal morphProgress READ morphProgress WRITE setMorphProgress)

public:
    enum State { Hidden, MorphingIn, Idle, Paging, Dismissing, MorphingOut, Deleting};

    explicit MediaViewer(QObject* parent = nullptr);

    /** @brief Initiates structural transition from the parent Grid layout. */
    void openAt(int index, const QRect& sourceGridRect, const QSize& screenSize);

    /** @brief Commands a geometric fallback to the original Grid thumbnail. */
    void closeTo(const QRect& targetGridRect);

    /** @brief Re-calibrates internal indices to prevent out-of-bounds rendering after batch edits. */
    void handleDeletion();

    /** @brief Explicitly tears down the decode pipeline to release physical memory. */
    void forceStopPlayback();

    /* --- Interaction Routing Pipeline --- */
    void onTap(const QPoint& pos);

    /**
     * @brief Routes continuous gestures based on the starting position.
     * @param start Absolute screen coordinate where touch began (Critical for arbitration).
     * @param dx Accumulated horizontal delta.
     * @param dy Accumulated vertical delta.
     */
    void onPanUpdate(const QPoint& currentPos, int deltaX, int deltaY);

    void onPanFinished(int vx, int vy);

    /* --- Render Pass --- */
    void paint(QPainter& p, const QRect& screenRect);

    /** @brief Allows GalleryView to utilize occlusion culling for underlying grids. */
    bool isFullyOpaque() const { return m_state == Idle || m_state == Paging ; }
    int currentIndex() const { return m_currentIndex; }
    State currentState() const { return m_state; }

    /* --- Animation Drivers --- */
    qreal slideOffset() const { return m_slideOffset; }
    void setSlideOffset(qreal o) { m_slideOffset = o; emit requestRedraw(); }
    qreal morphProgress() const { return m_morphProgress; }
    void setMorphProgress(qreal p) { m_morphProgress = p; emit requestRedraw(); }

signals:
    void requestRedraw();
    void closedCompletely();   ///< Fired when MorphOut finishes, trigger memory clear.
    void deleteRequested(int index);
    void dismissRequested();   ///< Asks GalleryView for the sync target rect.
    /** @brief Requests the underlying grid to silently align its scroll position to the given index. */
    void requestSync(int index);

private slots:
    void onMorphFinished();
    void onSlideFinished();

    /* Engine Callbacks */
    void onVideoFrameReady(const QImage& frame);
    void onVideoPositionChanged(qint64 posMs);

private:
    void prefetchImages();
    void syncVideoBarState();
    void drawImagePane(QPainter& p, int index, const QRectF& baseRect, const QRect& screen);

    struct Config {
        /* Timing & Physics */
        const int   MORPH_DUR_MS      = 350;
        const int   SNAP_DUR_MS       = 250;
        const qreal DISMISS_THRESHOLD_RATIO = 0.20;
        const qreal MIN_DISMISS_SCALE       = 0.65;
        const qreal BG_FADE_RESISTANCE      = 0.50;
        const qreal PAGE_GAP_RATIO          = 0.05;
        const qreal SWIPE_OFFSET_THRESHOLD  = 0.30;
        const float SWIPE_VELOCITY_THRESHOLD= 0.5f;
        const qreal GRID_CORNER_RADIUS_RATIO = 0.05;
    } m_cfg;

    State m_state = Hidden;
    int m_currentIndex = 0;
    QSize m_screenSize;

    /* Viewport Physics */
    qreal m_slideOffset = 0.0;
    qreal m_dragOffsetY = 0.0;

    /* Morphing Matrix Snapshots */
    QRect m_gridAnchorRect;
    QRectF m_dragReleaseRect;
    qreal m_morphProgress = 0.0;
    qreal m_morphStartBgAlpha = 1.0;
    qreal m_morphStartRadius = 0.0;
    bool m_wasPlayingBeforeScrub = false;
    bool m_hudIntendedVisible = true;

    /* Sub-Components (Zero-Widget Logic) */
    ViewerTopBar* m_topBar = nullptr;
    VideoControlBar* m_videoBar = nullptr;

    /* Animation Engines */
    QPropertyAnimation* m_morphAnim = nullptr;
    QPropertyAnimation* m_slideAnim = nullptr;

    /* Hardware Engine Context */
    VideoPlayer* m_player = nullptr;
    QImage m_currentVideoFrame;
    qint64 m_videoPositionMs = 0;
};

#endif // MEDIA_VIEWER_H
