#ifndef UI_OVERLAYS_PALETTE_SELECTOR_H
#define UI_OVERLAYS_PALETTE_SELECTOR_H

#include <QImage>
#include <QPoint>
#include <QPropertyAnimation>
#include <QVector>
#include <QWidget>

#include "processing/thermal_palette.h"
#include "ui/interaction_target.h"

class QPainter;

/**
 * @brief Full-screen pseudo-color selector overlay used by CameraView.
 *
 * - Manages one interactive selection session from present() to dismiss().
 * - Emits semantic selection events; rendering/persistence is handled by upper layers.
 */
class PaletteSelector : public QWidget, public InteractionTarget {
    Q_OBJECT
    Q_INTERFACES(InteractionTarget)
    Q_PROPERTY(qreal panelProgress READ panelProgress WRITE setPanelProgress)
    Q_PROPERTY(qreal centerIndex READ centerIndex WRITE setCenterIndex)

public:
    /* --- Lifecycle --- */
    explicit PaletteSelector(QWidget* parent = nullptr);
    ~PaletteSelector() override = default;

    /* --- Session Control --- */
    void present(ThermalPalette::Id initial);
    void dismiss(bool commitSelection);
    bool isPresented() const { return m_isPresented; }
    ThermalPalette::Id currentPalette() const;

    /* --- Preview Feed --- */
    void setPreviewFrame(ThermalPalette::Id id, const QImage& frame);
    void clearPreviewFrames();
    QSize previewFrameSize() const;

    /* --- InteractionTarget Contract --- */
    void onInteractionBegin(const InteractionEvent& event) override;
    InteractionUpdateDecision onInteractionUpdate(const InteractionEvent& event) override;
    void onInteractionEnd(const InteractionEvent& event) override;
    void onInteractionCancel() override;

    /* --- Animated Properties --- */
    qreal panelProgress() const { return m_panelProgress; }
    void setPanelProgress(qreal value);

    qreal centerIndex() const { return m_centerIndex; }
    void setCenterIndex(qreal value);

signals:
    /**
     * @brief Fired whenever center snap index changes during drag/animation.
     */
    void previewSelectionChanged(ThermalPalette::Id id);

    /**
     * @brief Fired when the selector closes and selection should be persisted.
     */
    void selectionCommitted(ThermalPalette::Id id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /* --- Rendering Helpers --- */
    bool buildCarouselLayout(const QRect& preview,
                             QVector<qreal>* outCenterOffsets,
                             QVector<qreal>* outCardWidths,
                             qreal* outFocusOffset) const;
    void paintMaskLayer(QPainter& p, const QRect& mask) const;
    void paintTitleLayer(QPainter& p, const QRect& title) const;
    void paintCardsLayer(QPainter& p,
                         const QRect& preview,
                         const QVector<qreal>& centerOffsets,
                         const QVector<qreal>& cardWidths,
                         qreal focusOffset) const;

    /* --- Geometry Helpers --- */
    QRect panelRect() const;
    QRect titleRect() const;
    QRect previewRect() const;
    QRect maskRect() const;
    qreal itemSpacingPx() const;
    int tappedIndex(const QPoint& pos) const;

    /* --- Index Helpers --- */
    int indexForPalette(ThermalPalette::Id id) const;
    ThermalPalette::Id paletteForIndex(int index) const;
    int nearestIndex(qreal value) const;
    qreal applyEdgeResistance(qreal candidate) const;
    void notifyPreviewSelection();
    QString displayName(ThermalPalette::Id id) const;

    /* --- Animation Engine --- */
    void animateSnapToIndex(int index);
    void animateSnapToNearest();
    void finishDismiss();

    enum class InteractionMode : quint8 {
        None,
        PreviewDrag,
        DismissDrag,
        OutsideTap,
        Passive
    };

    enum class DragAxis : quint8 {
        Unknown,
        Horizontal,
        Vertical
    };

    /* --- Runtime Palette Data --- */
    QVector<ThermalPalette::Id> m_paletteIds;
    QVector<QImage> m_previewFrames;

    /* --- Animation Engine --- */
    QPropertyAnimation* m_panelAnim = nullptr;
    QPropertyAnimation* m_snapAnim = nullptr;

    /* --- Session State --- */
    qreal m_panelProgress = 0.0;
    qreal m_centerIndex = 0.0;
    int m_lastPreviewIndex = -1;
    bool m_isPresented = false;
    bool m_commitOnClose = false;

    /* --- Interaction State --- */
    InteractionMode m_mode = InteractionMode::None;
    DragAxis m_dragAxis = DragAxis::Unknown;
    QPoint m_pressLocalPos;
    QPoint m_lastLocalPos;
    qreal m_dragStartCenterIndex = 0.0;
    qreal m_dragStartPanelProgress = 1.0;
    bool m_pressInPreview = false;

    /* --- Visual Configuration --- */
    struct LayoutConfig {
        static constexpr qreal PANEL_HEIGHT_RATIO = 0.46;
        static constexpr qreal TITLE_TOP_RATIO = 0.04;
        static constexpr qreal TITLE_HEIGHT_RATIO = 0.14;
        static constexpr qreal CARD_ASPECT = 1.65;
        static constexpr qreal SPACING_RATIO = 0.315;
        static constexpr qreal CENTER_CARD_FROM_SPACING = 1.02;
        static constexpr qreal GAP_FROM_BASE = 0.10;
        static constexpr qreal EDGE_RESISTANCE = 0.35;
        static constexpr qreal DRAG_SLOP_PX = 8.0;
        static constexpr qreal DISMISS_TAP_SLOP_PX = 14.0;
        static constexpr qreal DISMISS_DRAG_THRESHOLD_RATIO = 0.34;
        static constexpr qreal MASK_TOP_CUTOFF_RATIO = 0.42;
        static constexpr qreal MASK_CURVE_MID1_POS = 0.22;
        static constexpr qreal MASK_CURVE_MID2_POS = 0.52;
        static constexpr int MASK_CURVE_MID1_ALPHA = 120;
        static constexpr int MASK_CURVE_MID2_ALPHA = 210;
        static constexpr int MASK_CURVE_BOTTOM_ALPHA = 250;
        static constexpr int PANEL_ANIM_MS = 220;
        static constexpr int SNAP_ANIM_MS = 260;
    } m_cfg;
};

#endif // UI_OVERLAYS_PALETTE_SELECTOR_H
