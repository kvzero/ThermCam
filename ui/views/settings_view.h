#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include "ui/views/base_view.h"
#include "ui/settings_menu_types.h"
#include "core/settings_types.h"
#include "services/settings_service.h"
#include "ui/overlays/bubble_dialog.h"
#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QVector>

class QMouseEvent;
class ScrollIndicator;
class SettingsBaseRow;
class SettingsPrimaryRow;
class SettingsSecondaryRow;

/**
 * @brief Floating owner of SettingsView top controls and top-mask rendering.
 *
 * Lives and dies with SettingsView. Owns hit-testing and press-confirm contract
 * for Back/Close actions, and renders the non-widget black-to-transparent mask
 * required by the settings scroll interaction.
 */
class SettingsTopBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal maskOpacity READ maskOpacity WRITE setMaskOpacity)
public:
    /* --- Lifecycle --- */
    explicit SettingsTopBar(QWidget* parent = nullptr);

    /* --- Public Properties --- */
    void setTitle(const QString& title);
    qreal maskOpacity() const { return m_maskOpacity; }
    void setMaskOpacity(qreal v);

signals:
    /* --- Cross-Module Signals --- */
    void backTriggered();
    void closeTriggered();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class PressZone {
        None,
        Back,
        Close
    };

    PressZone zoneAt(const QPoint& pos) const;

    QString m_title = "Settings";
    qreal m_maskOpacity = 0.0;
    PressZone m_pressedZone = PressZone::None;
    QPoint m_lastPos;
    QRect m_backRect;
    QRect m_closeRect;
};

/**
 * @brief Layer-0 settings scene owner coordinating panel state, scroll state, and transitions.
 *
 * Owns the complete settings interaction lifecycle while active in App's view stack.
 * It receives Qt mouse gestures, controls row layout/animation, and emits
 * navigation intent back to App through EventBus.
 */
class SettingsView : public BaseView {
    Q_OBJECT
    Q_PROPERTY(qreal leftScroll READ leftScroll WRITE setLeftScroll)
    Q_PROPERTY(qreal rightScroll READ rightScroll WRITE setRightScroll)
    Q_PROPERTY(qreal splitProgress READ splitProgress WRITE setSplitProgress)

public:
    /* --- Lifecycle --- */
    explicit SettingsView(QWidget* parent = nullptr);
    ~SettingsView() override = default;

    void onEnter() override;
    void onExit() override;
    void handleKeyShortPress() override {}

    /* --- Public Properties --- */
    qreal leftScroll() const { return m_leftScroll; }
    qreal rightScroll() const { return m_rightScroll; }
    qreal splitProgress() const { return m_splitProgress; }

    void setLeftScroll(qreal v);
    void setRightScroll(qreal v);
    void setSplitProgress(qreal v);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onPrimaryRowActivated();
    void onSecondaryRowActivated();
    void onTopBarBackTriggered();
    void onTopBarCloseTriggered();
    void onSettingsApplyCompleted(const SettingsService::ApplyResult& result);

private:
    enum class PanelMode {
        Single,
        Expanded
    };

    enum class SwipeAxis {
        None,
        Horizontal,
        Vertical
    };

    enum class ScrollTarget {
        Left,
        Right
    };

    struct Config {
        const qreal TOPBAR_H_RATIO = 0.23;
        const qreal ROW_H_RATIO = 0.216;
        const qreal LEFT_PANEL_RATIO = 0.18;
        const qreal DIVIDER_WIDTH_RATIO = 0.0025;
        const qreal SWIPE_BACK_EDGE_RATIO = 0.10;
        const qreal SWIPE_BACK_DIST_RATIO = 0.15;
        const qreal OVERSCROLL_FRICTION = 0.35;
        const int   SNAP_DURATION_MS = 280;
        const int   DEADZONE_PX = 8;
    } m_cfg;

    /* --- UI Composition --- */
    void buildPrimaryRows();
    void rebuildSecondaryRows(int primaryIndex);
    void relayoutRows();
    void refreshTopMask();
    void refreshSecondaryRowsFromSnapshot(const SettingsSnapshot& snapshot);
    void refreshSecondaryRowsFromStore();
    void applyPatchFromUi(const SettingsPatch& patch);

    /* --- Layout & Scroll Math --- */
    int topBarHeight() const;
    int rowHeight() const;
    int leftPanelWidth() const;
    qreal leftMaxScroll() const;
    qreal rightMaxScroll() const;
    qreal applyOverscroll(qreal candidate, qreal maxScroll) const;
    void settleScroll(bool leftPanel, float velocity);
    SettingsBaseRow* rowAt(const QPoint& pos) const;
    void startPointerSession(const QPoint& pos, bool allowRowPress);
    void updatePointerSession(const QPoint& pos);
    void finishPointerSession(const QPoint& pos);
    void cancelPointerSession();
    void cancelActiveRowPress();
    void updateDragVelocity(const QPoint& pos);

    /* --- Bubble Overlay Flow --- */
    void initBubbles();
    void connectBubble(BubbleBase* bubble);
    void resizeBubbles();
    void raiseVisibleBubbles();
    void dismissBubblesImmediately();
    void showRadioListBubble(const RadioListBubble::Spec& spec,
                             const BubbleAnchorContext& anchor);
    void showSliderBubble(const SliderBubble::Spec& spec,
                          const BubbleAnchorContext& anchor);
    void showStepperBubble(const StepperBubble::Spec& spec,
                           const BubbleAnchorContext& anchor);
    void onBubbleOutsideDragStarted(const QPoint& startGlobal, const QPoint& currentGlobal);
    void onBubbleOutsideDragMoved(const QPoint& currentGlobal);
    void onBubbleOutsideDragReleased(const QPoint& finalGlobal);
    void onBubbleOutsideDragCanceled();

    /* --- Navigation & Panel Flow --- */
    void collapseToSingle();
    void expandPrimary(int primaryIndex);
    void triggerExitToCamera();

    /* --- Widget Ownership --- */
    SettingsTopBar* m_topBar = nullptr;
    ScrollIndicator* m_scrollIndicator = nullptr;
    RadioListBubble* m_radioListBubble = nullptr;
    SliderBubble* m_sliderBubble = nullptr;
    StepperBubble* m_stepperBubble = nullptr;

    /* --- Row Caches --- */
    QVector<SettingsPrimaryRow*> m_primaryRows;
    QVector<SettingsSecondaryRow*> m_secondaryRows;

    /* --- Panel State --- */
    PanelMode m_mode = PanelMode::Single;
    int m_activePrimary = -1;

    /* --- Animated Properties --- */
    qreal m_leftScroll = 0.0;
    qreal m_rightScroll = 0.0;
    qreal m_splitProgress = 0.0;

    /* --- Animation Engine --- */
    QPropertyAnimation* m_leftScrollAnim = nullptr;
    QPropertyAnimation* m_rightScrollAnim = nullptr;
    QPropertyAnimation* m_splitAnim = nullptr;

    /* --- Gesture Session State --- */
    bool m_pressActive = false;
    SettingsBaseRow* m_pressedRow = nullptr;
    QPoint m_pressStartPos;
    QPoint m_lastPos;
    QPoint m_previousSamplePos;
    QPointF m_velocityPxPerMs;
    QElapsedTimer m_velocityTimer;
    qint64 m_previousSampleMs = 0;
    SwipeAxis m_swipeAxis = SwipeAxis::None;
    ScrollTarget m_scrollTarget = ScrollTarget::Left;
    qreal m_dragStartLeft = 0.0;
    qreal m_dragStartRight = 0.0;

    /* --- Settings Apply Session --- */
    bool m_applyInFlight = false;
};

#endif // SETTINGS_VIEW_H
