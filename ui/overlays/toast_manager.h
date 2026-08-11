#ifndef TOAST_MANAGER_H
#define TOAST_MANAGER_H

#include <QWidget>
#include <QQueue>
#include <QPropertyAnimation>
#include <QTimer>
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
    ~ToastManager() override;

    /**
     * @brief Enqueues and displays a system notification.
     * @param msg The text payload.
     * @param level Determines the visual urgency indicator.
     */
    void showToast(const QString& msg, ToastLevel level = ToastLevel::Info);

    /* --- Property Accessors --- */
    int offsetY() const { return m_offsetY; }
    void setOffsetY(int y);

    /**
     * @brief Defines the interactive hit-box area.
     */
    QRect getVisualRect() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void processQueue();
    void animateOut();

private:
    void animateIn();
    void beginDrag(int globalY);
    void updateDrag(int globalY);
    void finishDrag();
    void cancelDrag();
    void releaseCurrentMouseReceiver(QObject* receiver, const QMouseEvent* sourceEvent);
    QRect visualRectGlobal() const;

    /* Data Pipeline */
    QQueue<ToastData> m_queue;
    ToastData m_current;
    bool m_isShowing = false;

    /* Physical State Machine */
    int m_baseY = 0;
    int m_offsetY = 0;
    int m_contentH = 0;

    /* Gesture Tracking */
    bool m_dragActive = false;
    bool m_sendingSyntheticRelease = false;
    int m_dragStartOffsetY = 0;
    int m_dragStartGlobalY = 0;

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
