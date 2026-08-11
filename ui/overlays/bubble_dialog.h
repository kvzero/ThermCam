#ifndef BUBBLE_DIALOG_H
#define BUBBLE_DIALOG_H

#include <QColor>
#include <QPoint>
#include <QPropertyAnimation>
#include <QRect>
#include <QString>
#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

#include <functional>

class QMouseEvent;
class QPainter;
class QTimer;

/**
 * @brief Placement context owned by the caller at presentation time.
 */
struct BubbleAnchorContext {
    QPoint pressPosGlobal;
    QRect triggerRectGlobal;
    QRect submenuRectGlobal;
    int submenuContentTopGlobalY = -1;
    int submenuContentBottomGlobalY = -1;
    int referenceRowHeightPx = 0;
};

/**
 * @brief Layer-2 lightweight bubble shell owner in App overlay stack.
 *
 * BubbleBase owns transparent full-screen hit routing, shared bubble material,
 * anchor-aware placement, and entry/exit lifecycle. Subclasses only provide
 * content rendering and content interaction contracts.
 */
class BubbleBase : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animProgress READ animProgress WRITE setAnimProgress)
    Q_PROPERTY(qreal touchProgress READ touchProgress WRITE setTouchProgress)

public:
    struct ContentLayout {
        QSize preferred;
    };

    /* --- Lifecycle --- */
    explicit BubbleBase(QWidget* parent = nullptr);
    ~BubbleBase() override = default;

    /* --- Presentation API --- */
    void present(const BubbleAnchorContext& anchor);
    void dismiss();
    void dismissImmediately();

    /* --- Animation Properties --- */
    qreal animProgress() const { return m_animProgress; }
    void setAnimProgress(qreal p) { m_animProgress = p; update(); }
    qreal touchProgress() const { return m_touchProgress; }
    void setTouchProgress(qreal p) { m_touchProgress = p; update(); }

signals:
    /* --- Cross-Module Signals --- */
    /** @brief Fired after exit animation completes and the bubble is fully hidden. */
    void bubbleDismissed();
    void outsideDragStarted(const QPoint& startGlobal, const QPoint& currentGlobal);
    void outsideDragMoved(const QPoint& currentGlobal);
    void outsideDragReleased(const QPoint& finalGlobal);
    void outsideDragCanceled();

protected:
    /* --- Content Extension Points --- */
    virtual ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                            const QSize& viewportSize) const = 0;
    virtual void paintContent(QPainter& p, const QRect& contentRect) = 0;
    virtual bool contentPress(const QPoint& contentPos);
    virtual bool contentMove(const QPoint& contentPos);
    virtual bool contentRelease(const QPoint& contentPos);
    virtual void contentCancel();
    virtual void onBubbleShown() {}
    virtual void onBubbleDismissed() {}

    const BubbleAnchorContext& anchorContext() const { return m_anchor; }
    QRect bubbleRect() const { return m_panelRect; }
    QRect contentRect() const { return m_contentRect; }
    qreal bubbleCornerRadius() const { return m_cfg.BOX_CORNER_RADIUS; }
    int referenceRowHeight() const { return qMax(1, m_anchor.referenceRowHeightPx); }

    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /* --- Interaction Model --- */
    enum class PressTarget {
        None,
        Outside,
        Bubble,
        Content
    };

    enum class ExpandDirection {
        Down,
        Up
    };

    /* --- Layout & Interaction Helpers --- */
    bool updatePress(const QPoint& localPos);
    void beginPress(const QPoint& localPos);
    void endPress(bool allowAction);
    void clearPressState();
    void setInteractionActive(bool active, const QPoint& localPos = QPoint());
    void relayout();
    bool shouldReleaseOwnershipForOutsidePan(const QPoint& localPos) const;
    void finishDismiss();
    void onPopAnimFinished();

    /* --- Visual Config --- */
    struct Config {
        const qreal PANEL_TO_SUBMENU_W_RATIO = 0.90;
        const qreal PANEL_W_MIN_RATIO = 0.28;
        const qreal PANEL_W_MAX_RATIO = 0.94;
        const qreal PANEL_H_MAX_RATIO = 0.92;

        const qreal CONTENT_PAD_X_RATIO = 0.018;
        const qreal CONTENT_PAD_Y_ROW_RATIO = 0.14;
        const qreal CONTENT_PAD_Y_MIN_RATIO = 0.010;

        const qreal BOX_CORNER_RADIUS = 42.0;
        const qreal SCALE_POP_START = 0.84;
        const qreal SCALE_TOUCH_MAX = 1.02;

        const int OUTSIDE_PAN_THRESHOLD_PX = 2;
        const int DURATION_POP_MS = 230;
        const int DURATION_EXIT_MS = 120;
        const int DURATION_TOUCH_MS = 130;

        const QColor BOX_BG_START = QColor(35, 35, 35, 245);
        const QColor BOX_BG_END = QColor(20, 20, 20, 255);
        const QColor BOX_STROKE = QColor(255, 255, 255, 55);
        const QColor GLOW_COLOR = QColor(255, 255, 255, 45);
    } m_cfg;

    /* --- Runtime State --- */
    BubbleAnchorContext m_anchor;

    QRect m_panelRect;
    QRect m_contentRect;
    QPointF m_scaleOrigin;
    ExpandDirection m_direction = ExpandDirection::Down;

    PressTarget m_pressedTarget = PressTarget::None;
    QPoint m_pressStartPos;
    QPoint m_lastPos;
    QPoint m_glowPos;
    bool m_isPanelPressed = false;
    bool m_isDismissing = false;
    bool m_releasedByOutsidePan = false;
    bool m_forwardingOutsideDrag = false;

    /* --- Animation Engine --- */
    qreal m_animProgress = 0.0;
    qreal m_touchProgress = 0.0;
    QPropertyAnimation* m_popAnim = nullptr;
    QPropertyAnimation* m_touchAnim = nullptr;
};

/**
 * @brief BubbleBase payload for single-choice list selection.
 *
 * Owns option hit-testing, pressed-highlight rendering, and optional overflow
 * scrolling while BubbleBase handles shell lifecycle and outside gestures.
 */
class RadioListBubble : public BubbleBase {
    Q_OBJECT
    Q_PROPERTY(qreal scrollOffset READ scrollOffset WRITE setScrollOffset)

public:
    struct Item {
        QString id;
        QString text;
    };

    /* --- Caller Contract --- */
    struct Spec {
        QVector<Item> items;
        int selectedIndex = -1;
        bool dismissOnSelection = true;
        std::function<void(int index, const QString& id)> onSelected;
        std::function<void()> onDismissed;
    };

    /* --- Lifecycle --- */
    explicit RadioListBubble(QWidget* parent = nullptr);
    ~RadioListBubble() override = default;

    /* --- Presentation API --- */
    void present(const Spec& spec, const BubbleAnchorContext& anchor);

    /* --- Animated Properties --- */
    qreal scrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(qreal value);

protected:
    /* --- BubbleBase Content Contract --- */
    ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                    const QSize& viewportSize) const override;
    void paintContent(QPainter& p, const QRect& contentRect) override;
    bool contentPress(const QPoint& contentPos) override;
    bool contentMove(const QPoint& contentPos) override;
    bool contentRelease(const QPoint& contentPos) override;
    void contentCancel() override;
    void onBubbleDismissed() override;

private:
    /* --- Internal Helpers --- */
    int rowHeightPx() const;
    int rowIndexAt(const QPoint& contentPos) const;
    qreal totalRowsHeight() const;
    qreal maxScroll() const;
    void settleScrollIfNeeded();
    qreal applyOverscroll(qreal candidate) const;

    /* --- Runtime State --- */
    Spec m_spec;

    int m_pressedIndex = -1;
    int m_selectedIndex = -1;
    bool m_dragScrolling = false;
    QPoint m_contentPressPos;
    QPoint m_contentLastPos;
    qreal m_scrollStartOffset = 0.0;
    qreal m_scrollOffset = 0.0;

    QPropertyAnimation* m_scrollAnim = nullptr;

    /* --- Visual Config --- */
    struct Config {
        const qreal ROW_H_REF_RATIO = 0.78;
        const qreal ROW_TEXT_SIZE_RATIO = 0.42;
        const qreal ROW_SIDE_PAD_RATIO = 0.08;
        const qreal PRESS_INSET_RATIO = 0.08;
        const qreal PRESS_RADIUS_OUTER_RATIO = 0.82;
        const qreal PRESS_RADIUS_ROW_CAP_RATIO = 0.56;
        const qreal PRESS_RADIUS_MIN_PX = 10.0;
        const QColor PRESS_OVERLAY = QColor(255, 255, 255, 33);
        const QColor TEXT_COLOR = Qt::white;
        const QColor CHECK_COLOR = QColor(87, 186, 255);
        const int SCROLL_DRAG_THRESHOLD_PX = 6;
        const int SCROLL_SETTLE_MS = 160;
    } m_cfg;
};

/**
 * @brief BubbleBase payload for icon + horizontal value slider interaction.
 *
 * Owns slider geometry, pointer-to-value mapping, and discrete-step commit
 * semantics while BubbleBase remains the owner of shell lifecycle.
 */
class SliderBubble : public BubbleBase {
    Q_OBJECT

public:
    /* --- Caller Contract --- */
    struct Spec {
        QString iconGlyph;
        QColor accentColor = QColor(87, 186, 255);
        int minValue = 0;
        int maxValue = 100;
        int step = 1;
        int value = 50;
        bool dismissOnCommit = true;
        std::function<void(int value)> onValueChanging;
        std::function<void(int value)> onValueCommitted;
        std::function<void()> onDismissed;
    };

    /* --- Lifecycle --- */
    explicit SliderBubble(QWidget* parent = nullptr);
    ~SliderBubble() override = default;

    /* --- Presentation API --- */
    void present(const Spec& spec, const BubbleAnchorContext& anchor);

protected:
    /* --- BubbleBase Content Contract --- */
    ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                    const QSize& viewportSize) const override;
    void paintContent(QPainter& p, const QRect& contentRect) override;
    bool contentPress(const QPoint& contentPos) override;
    bool contentMove(const QPoint& contentPos) override;
    bool contentRelease(const QPoint& contentPos) override;
    void contentCancel() override;
    void onBubbleDismissed() override;

private:
    /* --- Internal Helpers --- */
    int normalizeValue(int value) const;
    int valueFromTrackX(int x) const;
    qreal valueToRatio(int value) const;
    void recalcGeometry(const QRect& contentRect);
    bool isTrackInteractive(const QPoint& contentPos) const;
    void updateValueByPointer(const QPoint& contentPos, bool notifyChanging);

    /* --- Runtime State --- */
    Spec m_spec;
    int m_value = 0;
    bool m_sliderActive = false;
    bool m_hasValueChanged = false;
    QRect m_iconRect;
    QRect m_trackRect;
    QRect m_trackHitRect;

    /* --- Visual Config --- */
    struct Config {
        const qreal ICON_SLOT_RATIO = 0.21;
        const qreal ICON_SIZE_RATIO = 0.64;
        const qreal TRACK_H_RATIO = 0.16;
        const qreal TRACK_HIT_PAD_RATIO = 0.32;
        const qreal TRACK_HIT_PAD_X_RATIO = 0.16;
        const qreal TRACK_GAP_RATIO = 0.03;
        const qreal TRACK_END_PAD_RATIO = 0.07;
        const qreal TRACK_EDGE_SNAP_RATIO = 0.10;
        const qreal FONT_RATIO = 0.9;
        const int TRACK_EDGE_SNAP_MIN_PX = 12;

        const QColor ICON_COLOR = Qt::white;
        const QColor TRACK_BG = QColor(255, 255, 255, 72);
        const QColor HANDLE_COLOR = Qt::white;
    } m_cfg;
};

/**
 * @brief BubbleBase payload for +/- step controls with hold-to-repeat acceleration.
 *
 * Owns increment/decrement button hit-testing, hold repeat timing, value clamping,
 * and commit callbacks while BubbleBase keeps shell lifecycle behavior.
 */
class StepperBubble : public BubbleBase {
    Q_OBJECT

public:
    struct Spec {
        int minValue = 0;
        int maxValue = 100;
        int step = 1;
        int value = 0;
        bool dismissOnCommit = false;
        QString minusGlyph = QString(QChar(0xfb40));
        QString plusGlyph = QString(QChar(0xf6e8));
        std::function<QString(int value)> valueTextFormatter;
        std::function<void(int value)> onValueChanging;
        std::function<void(int value)> onValueCommitted;
        std::function<void()> onDismissed;
    };

    explicit StepperBubble(QWidget* parent = nullptr);
    ~StepperBubble() override = default;

    void present(const Spec& spec, const BubbleAnchorContext& anchor);

protected:
    ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                    const QSize& viewportSize) const override;
    void paintContent(QPainter& p, const QRect& contentRect) override;
    bool contentPress(const QPoint& contentPos) override;
    bool contentMove(const QPoint& contentPos) override;
    bool contentRelease(const QPoint& contentPos) override;
    void contentCancel() override;
    void onBubbleDismissed() override;

private:
    enum class Zone {
        None,
        Minus,
        Plus
    };

    int normalizeValue(int value) const;
    void recalcGeometry(const QRect& contentRect);
    Zone zoneAt(const QPoint& contentPos) const;
    void applyStep(int direction, bool notifyChanging);
    void startRepeat();
    void stopRepeat();
    int repeatIntervalMs() const;
    void commitIfDirty();
    QString formattedValueText() const;

    Spec m_spec;

    int m_value = 0;
    int m_committedValue = 0;
    bool m_dirtyFromCommitted = false;
    Zone m_activeZone = Zone::None;

    QTimer* m_repeatStartTimer = nullptr;
    QTimer* m_repeatTimer = nullptr;
    QElapsedTimer m_holdElapsed;

    QRect m_minusRect;
    QRect m_plusRect;
    QRect m_valueRect;

    struct Config {
        const qreal HEIGHT_REF_RATIO = 0.92;
        const qreal BUTTON_SIZE_RATIO = 0.86;
        const qreal SIDE_PAD_RATIO = 0.02;
        const qreal GAP_RATIO = 0.04;
        const qreal ICON_SIZE_RATIO = 0.84;
        const qreal VALUE_FONT_RATIO = 0.42;

        const int REPEAT_START_DELAY_MS = 380;
        const int REPEAT_SLOW_INTERVAL_MS = 150;
        const int REPEAT_FAST_INTERVAL_MS = 65;
        const int REPEAT_FAST_AFTER_MS = 1800;

        const QColor BTN_ICON = Qt::white;
        const QColor VALUE_TEXT = Qt::white;
    } m_cfg;
};

#endif // BUBBLE_DIALOG_H
