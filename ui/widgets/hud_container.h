#ifndef HUD_CONTAINER_H
#define HUD_CONTAINER_H

#include <QWidget>

class QPropertyAnimation;
class QResizeEvent;
class StatusBar;
class CapsuleButton;
class ModeSelector;

/**
 * @brief Layer 1: The sliding interaction layer containing HUD elements.
 *
 * Implements a physical "Grab-and-Release" model with intent persistence.
 * This allows the HUD to resume its movement if interrupted by a static tap.
 */
class HudContainer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QPoint pos READ pos WRITE move)

public:
    explicit HudContainer(QWidget* parent = nullptr);
    virtual ~HudContainer() = default;

    /**
     * @brief Grab the HUD: Interrupts ongoing motion and captures the current position.
     */
    void stopAnimations();

    /**
     * @brief Passively follow the finger displacement.
     * @param dx Accumulated horizontal displacement from the touch-down anchor.
     */
    void followGesture(int dx);

    /**
     * @brief Finalize the physical state on finger lift.
     * @param vx Final horizontal velocity in px/ms.
     */
    void finalizeGesture(int finalDx, float vx);

    /** @brief Reset the HUD to the default visible state (0,0). */
    void resetPosition();

    /* --- Hit-Testing Accessors --- */
    QWidget* capsuleButton() const { return (QWidget*)m_capsuleBtn; }
    QWidget* modeButton() const { return (QWidget*)m_modeSelector; }

    bool isHidden() const { return m_isHidden; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    /* UI Components */
    StatusBar* m_statusBar = nullptr;
    CapsuleButton* m_capsuleBtn = nullptr;
    ModeSelector* m_modeSelector  = nullptr;

    /* Physical State Machine */
    QPropertyAnimation* m_snapAnimation = nullptr;
    int  m_dragAnchorX = 0;   /**< HUD X-coordinate at the moment of Grab */
    int  m_targetX = 0;       /**< Persistent intended destination (0 or width) */
    bool m_isHidden = false;
    bool m_wasMoved = false; /**< Tracks if the current session involved significant movement */

    /* Constants: Physics & Layout */
    static constexpr qreal STATUS_BAR_H_RATIO      = 0.10;
    static constexpr int   GESTURE_DEADZONE_PX     = 10;
    static constexpr qreal SNAP_THRESHOLD_RATIO    = 0.10;
    static constexpr int   ANIMATION_DURATION_MS   = 500;
};

#endif // HUD_CONTAINER_H
