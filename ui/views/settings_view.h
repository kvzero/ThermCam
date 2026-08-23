#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include "ui/views/base_view.h"
#include "ui/settings_catalog.h"
#include "core/settings_types.h"
#include "services/settings_service.h"
#include "ui/overlays/bubble_dialog.h"
#include <QElapsedTimer>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QVector>
#include <optional>

class QMouseEvent;
class ScrollIndicator;
class SettingsBaseRow;
class SettingsPrimaryRow;
class SettingsItemRow;
class SettingsPageBackdrop;

/**
 * @brief Floating owner of SettingsView top controls and top-mask rendering.
 *
 * Lives and dies with SettingsView. Owns hit-testing and press-confirm contract
 * for Back/Close actions, and renders the non-widget black-to-transparent mask
 * required by the settings scroll interaction.
 */
class SettingsTopBar : public QWidget {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    explicit SettingsTopBar(QWidget* parent = nullptr);

    /* --- Public Properties --- */
    void setTitle(const QString& title);

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

    QString m_title;
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
    Q_PROPERTY(qreal pageScroll READ pageScroll WRITE setPageScroll)
    Q_PROPERTY(qreal splitProgress READ splitProgress WRITE setSplitProgress)
    Q_PROPERTY(qreal rootRetreatProgress READ rootRetreatProgress WRITE setRootRetreatProgress)
    Q_PROPERTY(qreal pageEntranceProgress READ pageEntranceProgress WRITE setPageEntranceProgress)

public:
    /* --- Lifecycle --- */
    explicit SettingsView(QWidget* parent = nullptr);
    ~SettingsView() override = default;

    void onEnter() override;
    void onExit() override;
    void handleKeyShortPress() override {}

    /**
     * @brief Opens the section containing item and brings its row into view.
     *
     * This is navigation only: it never activates the item or changes a value.
     */
    void openItem(SettingID item);

    /* --- Public Properties --- */
    qreal leftScroll() const { return m_leftScroll; }
    qreal rightScroll() const { return m_sectionItems.scroll; }
    qreal pageScroll() const { return m_pageItems.scroll; }
    qreal splitProgress() const { return m_splitProgress; }
    qreal rootRetreatProgress() const { return m_rootRetreatProgress; }
    qreal pageEntranceProgress() const { return m_pageEntranceProgress; }

    void setLeftScroll(qreal v);
    void setRightScroll(qreal v);
    void setPageScroll(qreal v);
    void setSplitProgress(qreal v);
    void setRootRetreatProgress(qreal v);
    void setPageEntranceProgress(qreal v);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onPrimaryRowActivated();
    void onItemRowActivated();
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
        Right,
        Page
    };

    struct Config {
        const qreal TOPBAR_H_RATIO = 0.23;
        const qreal ROW_H_RATIO = 0.216;
        const qreal LEFT_PANEL_RATIO = 0.18;
        const qreal DIVIDER_WIDTH_RATIO = 0.0025;
        const qreal SWIPE_BACK_EDGE_RATIO = 0.10;
        const qreal SWIPE_BACK_DIST_RATIO = 0.15;
        const qreal OVERSCROLL_FRICTION = 0.35;
        const qreal ROOT_PAGE_RETREAT_RATIO = 0.16;
        const int   SNAP_DURATION_MS = 280;
        const int   ROOT_PAGE_RETREAT_MS = 320;
        const int   PAGE_ENTER_MS = 220;
        const int   DEADZONE_PX = 8;
    } m_cfg;

    /* --- UI Composition --- */
    void buildPrimaryRows();
    void rebuildSectionItems(int primaryIndex);
    void rebuildPageItems(SettingsSection section);
    void rebuildItemRows(QVector<SettingsItemRow*>& rows,
                         const std::vector<SettingsItemData>& items);
    void relayoutRows();
    void layoutItemRows(const QVector<SettingsItemRow*>& rows,
                        qreal scroll,
                        int x,
                        int width,
                        bool visible);
    void refreshItemRowsFromSnapshot(QVector<SettingsItemRow*>& rows,
                                     const SettingsSnapshot& snapshot);
    void refreshItemRowsFromStore();
    void refreshStorageRowsIfVisible();
    void refreshLanguage();
    void applyPatchFromUi(const SettingsPatch& patch);
    void activateSettingItem(SettingsItemRow* row, const SettingsItemData& item);
    void activateCommandItem(const SettingsItemData& item);

    /* --- Layout & Scroll Math --- */
    int topBarHeight() const;
    int rowHeight() const;
    int leftPanelWidth() const;
    qreal leftMaxScroll() const;
    qreal rightMaxScroll() const;
    qreal pageMaxScroll() const;
    qreal itemRowsMaxScroll(const QVector<SettingsItemRow*>& rows) const;
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
    void openPage(SettingsSection section, const QString& title);
    void closePage();
    void resetPageImmediately();
    void triggerExitToCamera();

    /* --- Widget Ownership --- */
    SettingsTopBar* m_topBar = nullptr;
    ScrollIndicator* m_scrollIndicator = nullptr;
    RadioListBubble* m_radioListBubble = nullptr;
    SliderBubble* m_sliderBubble = nullptr;
    StepperBubble* m_stepperBubble = nullptr;
    // Child overlay, not a viewport: rows remain direct children and can scroll below the top bar.
    // It owns the page-layer background and paints the shared scroll indicator above that background.
    SettingsPageBackdrop* m_pageBackdrop = nullptr;

    /* --- Row Caches --- */
    QVector<SettingsPrimaryRow*> m_primaryRows;
    // Both containers are owned by this view so their rows may move beneath the fixed top bar.
    struct ItemRows {
        QVector<SettingsItemRow*> rows;
        qreal scroll = 0.0;
    };
    ItemRows m_sectionItems;
    ItemRows m_pageItems;

    /* --- Panel State --- */
    PanelMode m_mode = PanelMode::Single;
    int m_activePrimary = -1;
    std::optional<SettingsSection> m_activePage;

    /* --- Animated Properties --- */
    qreal m_leftScroll = 0.0;
    qreal m_splitProgress = 0.0;
    qreal m_rootRetreatProgress = 0.0;
    qreal m_pageEntranceProgress = 0.0;

    /* --- Animation Engine --- */
    QPropertyAnimation* m_leftScrollAnim = nullptr;
    QPropertyAnimation* m_rightScrollAnim = nullptr;
    QPropertyAnimation* m_pageScrollAnim = nullptr;
    QPropertyAnimation* m_splitAnim = nullptr;
    QPropertyAnimation* m_rootRetreatAnim = nullptr;
    QPropertyAnimation* m_pageEntranceAnim = nullptr;
    QParallelAnimationGroup* m_pageTransition = nullptr;
    bool m_pageTransitionInFlight = false;

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
    qreal m_dragStartPage = 0.0;

    /* --- Settings Apply Session --- */
    bool m_applyInFlight = false;
};

#endif // SETTINGS_VIEW_H
