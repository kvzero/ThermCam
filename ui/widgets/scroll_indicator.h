#ifndef SCROLL_INDICATOR_H
#define SCROLL_INDICATOR_H

#include <QObject>
#include <QPropertyAnimation>
#include <QTimer>
#include <QRect>
#include <QPainter>
#include <QColor>

/**
 * @brief High-performance, zero-widget scrollbar renderer.
 *
 * Designed for low-RAM embedded systems. It computes physical overscroll
 * compression and fade-in/out animations without creating a new window layer.
 * Must be manually rendered inside the parent's paintEvent.
 */
class ScrollIndicator : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
    explicit ScrollIndicator(QObject* parent = nullptr);

    /**
     * @brief Feeds the latest scroll physics into the indicator.
     * Automatically awakens the indicator and resets the fade-out timer.
     *
     * @param currentScroll The active scroll offset (can be negative or > maxScroll).
     * @param maxScroll The boundary of the scrollable content.
     */
    void updateState(qreal currentScroll, qreal maxScroll);

    /**
     * @brief Renders the indicator onto the parent's canvas.
     * Computes geometry, compression, and alpha blending in real-time.
     */
    void paint(QPainter& p, const QRect& viewRect);

    /* Animation Property */
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o);

signals:
    /**
     * @brief Fired during the fade-out animation.
     * The parent view MUST connect this to its own update() slot to redraw.
     */
    void opacityChanged();

private slots:
    void initiateFadeOut();

private:
    struct Config {
        /* Aesthetics */
        const int   WIDTH             = 6;      // Thickness of the bar
        const int   MARGIN_RIGHT      = 2;      // Distance from the right edge
        const int   MIN_HEIGHT        = 48;     // Prevents the bar from becoming too tiny
        const QColor COLOR_BASE       = QColor(255, 255, 255, 160); // Base semi-transparent white

        /* Dynamics & Timing */
        const int   FADE_DELAY_MS     = 1000;   // Idle time before fading starts
        const int   FADE_DURATION_MS  = 350;    // Duration of the fade animation
        const qreal COMPRESS_FACTOR   = 0.6;   // How strongly the bar squishes during overscroll
    } m_cfg;

    /* Physics State */
    qreal m_currentScroll = 0.0;
    qreal m_maxScroll     = 0.0;

    /* Visual State */
    qreal m_opacity       = 0.0;

    /* Animation Components */
    QTimer* m_hideTimer = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
};

#endif // SCROLL_INDICATOR_H
