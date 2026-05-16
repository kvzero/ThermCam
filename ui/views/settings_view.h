#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include "ui/views/base_view.h"
#include "ui/settings_menu_types.h"
#include <QPropertyAnimation>
#include <QVector>

class ScrollIndicator;
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

    /* --- UIController Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);
    Q_INVOKABLE void cancelGesture();

signals:
    /* --- Cross-Module Signals --- */
    void backTriggered();
    void closeTriggered();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
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
 * It receives semantic gestures from InteractionArbiter, controls row layout/animation,
 * and emits navigation intent back to App through EventBus.
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

    /* --- UIController Protocol --- */
    void onGestureStarted() override;
    void onGestureUpdate(const QPoint& start, int dx, int dy) override;
    void onGestureFinished(const QPoint& start, int dx, int dy, float vx, float vy) override;

    /* --- Public Properties --- */
    qreal leftScroll() const { return m_leftScroll; }
    qreal rightScroll() const { return m_rightScroll; }
    qreal splitProgress() const { return m_splitProgress; }

    void setLeftScroll(qreal v);
    void setRightScroll(qreal v);
    void setSplitProgress(qreal v);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onPrimaryRowActivated();
    void onSecondaryRowActivated();
    void onTopBarBackTriggered();
    void onTopBarCloseTriggered();

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

    /* --- Layout & Scroll Math --- */
    int topBarHeight() const;
    int rowHeight() const;
    int leftPanelWidth() const;
    qreal leftMaxScroll() const;
    qreal rightMaxScroll() const;
    qreal applyOverscroll(qreal candidate, qreal maxScroll) const;
    void settleScroll(bool leftPanel, float velocity);

    /* --- Navigation & Panel Flow --- */
    void collapseToSingle();
    void expandPrimary(int primaryIndex);
    void triggerExitToCamera();

    /* --- Widget Ownership --- */
    SettingsTopBar* m_topBar = nullptr;
    ScrollIndicator* m_scrollIndicator = nullptr;

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
    SwipeAxis m_swipeAxis = SwipeAxis::None;
    ScrollTarget m_scrollTarget = ScrollTarget::Left;
    qreal m_dragStartLeft = 0.0;
    qreal m_dragStartRight = 0.0;
    int m_lastDx = 0;
    int m_lastDy = 0;
};

#endif // SETTINGS_VIEW_H
