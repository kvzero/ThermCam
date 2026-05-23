#ifndef TOAST_MANAGER_H
#define TOAST_MANAGER_H

#include <QWidget>
#include <QQueue>
#include <QPropertyAnimation>
#include <QTimer>
#include "core/types.h"
#include "ui/interaction_target.h"

/**
 * @brief Global Notification Overlay (Layer 3).
 *
 * Implements a non-blocking, queue-based notification system.
 * Features a resilient physical interaction model including drag-to-dismiss
 * and rubber-band damping.
 */
class ToastManager : public QWidget, public InteractionTarget {
    Q_OBJECT
    Q_INTERFACES(InteractionTarget)
    Q_PROPERTY(int offsetY READ offsetY WRITE setOffsetY)

public:
    struct ToastData {
        QString message;
        ToastLevel level;
    };

    explicit ToastManager(QWidget* parent = nullptr);
    ~ToastManager() override = default;

    /**
     * @brief Enqueues and displays a system notification.
     * @param msg The text payload.
     * @param level Determines the visual urgency indicator.
     */
    void showToast(const QString& msg, ToastLevel level = ToastLevel::Info);

    /* --- Physical Interaction Protocol --- */

//    /**
//     * @brief Tracks continuous vertical displacement.
//     * Applies linear tracking upwards, and square-root damping downwards.
//     */
//    Q_INVOKABLE void followGesture(int dy);

    /** @brief InteractionTarget update path; Toast keeps ownership while visible. */
    InteractionUpdateDecision onInteractionUpdate(const InteractionEvent& event) override;

    /** @brief Completes the interaction and decides dismiss vs snap-back. */
    void onInteractionEnd(const InteractionEvent& event) override;
    void onInteractionCancel() override;
    void onInteractionBegin(const InteractionEvent& event) override;

    /* --- Property Accessors --- */
    int offsetY() const { return m_offsetY; }
    void setOffsetY(int y);

    /**
     * @brief Defines the interactive hit-box area.
     */
    QRect getVisualRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void processQueue();
    void animateOut();

private:
    void animateIn();

    /* Data Pipeline */
    QQueue<ToastData> m_queue;
    ToastData m_current;
    bool m_isShowing = false;

    /* Physical State Machine */
    int m_baseY = 0;
    int m_offsetY = 0;
    int m_contentH = 0;

    /* Gesture Tracking*/
    int m_gestureStartOffsetY = 0;     // offsetY when gesture started on this widget
    int m_gestureStartY = 0;           // 手势开始时的全局绝对Y坐标
    bool m_isFirstGestureMove = true;  // 标记是否需要同步起始锚点

    /* Animation Components */
    QPropertyAnimation* m_anim = nullptr;
    QTimer* m_autoHideTimer = nullptr;

    /* Configuration Constants */
    static constexpr qreal WIDTH_RATIO             = 0.70;
    static constexpr qreal HEIGHT_RATIO            = 0.10;
    static constexpr qreal TOP_MARGIN_RATIO        = 0.05;
    static constexpr qreal DISMISS_THRESHOLD_RATIO = 0.40;
    static constexpr double DAMPING_FACTOR         = 3.0;

    static constexpr int ANIM_DURATION_MS          = 300;
    static constexpr int DISPLAY_DURATION_MS       = 3000;
    static constexpr int RESUME_DURATION_MS        = 2000;
    static constexpr int DISMISS_OFFSET_PX         = 20;
};

#endif // TOAST_MANAGER_H
