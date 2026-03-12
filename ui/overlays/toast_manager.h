#ifndef TOAST_MANAGER_H
#define TOAST_MANAGER_H

#include <QWidget>
#include <QQueue>
#include <QPropertyAnimation>
#include <QTimer>
#include <QMouseEvent>
#include "core/types.h"

/**
 * @brief Global Notification Overlay (Layer 3).
 *
 * Implements a non-blocking, queue-based notification system.
 * Features a resilient physical interaction model including drag-to-dismiss
 * and rubber-band damping.
 */
class ToastManager : public QWidget {
    Q_OBJECT
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

    /**
     * @brief 高级交互协议：处理位置更新并始终消费事件。
     * @param localPos 手指在Toast控件坐标系下的位置。
     * @return 始终返回 true，因为Toast一旦被抓住，就拥有最高优先级。
     */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);

    /**
     * @brief Evaluates the final position to trigger dismissal or snap-back.
     */
    Q_INVOKABLE void finalizeGesture(int finalDy);

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
    void mousePressEvent(QMouseEvent* event) override;

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
