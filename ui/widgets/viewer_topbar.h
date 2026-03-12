#ifndef VIEWER_TOPBAR_H
#define VIEWER_TOPBAR_H

#include <QObject>
#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
#include <QRect>
#include <QPainter>
#include "core/types.h"

/**
 * @brief Render helper for the Media Viewer's top control bar.
 * Handles auto-hiding, hit-testing, and drawing the Back/Delete actions.
 * Operates purely in physical coordinates without QWidget overhead.
 */
class ViewerTopBar : public QWidget  {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(qreal backGlow READ backGlow WRITE setBackGlow)
    Q_PROPERTY(qreal deleteGlow READ deleteGlow WRITE setDeleteGlow)
    Q_PROPERTY(qreal timeGlow READ timeGlow WRITE setTimeGlow)

public:
    explicit ViewerTopBar(QWidget* parent = nullptr);

    /** @brief Awaken the top bar (fade in) and reset the auto-hide timer */
    void show();
    /** @brief Force immediate hide without animation (e.g., during pinch/dismiss) */
    void hideImmediate();

    /** @brief Update the displayed file metadata */
    void updateInfo(const MediaFileInfo& info);

    /** --- Interaction Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy = 0);

    /** @brief Main render pass */
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o) { m_opacity = o; update(); }

    qreal backGlow() const { return m_backGlow; }
    void setBackGlow(qreal g) { m_backGlow = g; update(); }

    qreal deleteGlow() const { return m_deleteGlow; }
    void setDeleteGlow(qreal g) { m_deleteGlow = g; update(); }

    qreal timeGlow() const { return m_timeGlow; }
    void setTimeGlow(qreal g) { m_timeGlow = g; update(); }

signals:
    void backRequested();
    void deleteRequested();
    void interactionActive();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void initiateFadeOut();

private:
    struct Config {
        const int   HEIGHT_RATIO      = 15;
        const int   MARGIN_RATIO      = 3;
        const int   FADE_DELAY_MS     = 3000;
        const int   FADE_DURATION_MS  = 250;

        const QColor BG_COLOR         = QColor(40, 40, 40, 210);
        const QColor STROKE_COLOR     = QColor(255, 255, 255, 45);
        const QColor DANGER_COLOR     = QColor(255, 59, 48);
        const QColor GLOW_WHITE       = QColor(255, 255, 255, 80);
        const QString ICON_BACK       = QChar(0xea60);
        const QString ICON_TRASH      = QChar(0xeb41);
    } m_cfg;

    QTimer* m_hideTimer = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
    qreal m_opacity = 0.0;
    MediaFileInfo m_currentInfo;

    /* Hit Testing Geometry */
    QRectF m_backRect;
    QRectF m_deleteRect;
    bool m_hoverBack = false;
    bool m_hoverDelete = false;

    // --- Additional Hit Testing Geometry & Glow State ---
    QRectF m_timeRect; // Store the time text rect for hit testing
    bool m_hoverTime = false;

    QPointF m_lastGlowPos;
    qreal m_backGlow = 0.0;
    qreal m_deleteGlow = 0.0;
    qreal m_timeGlow = 0.0;

    QPropertyAnimation* m_backGlowAnim = nullptr;
    QPropertyAnimation* m_deleteGlowAnim = nullptr;
    QPropertyAnimation* m_timeGlowAnim = nullptr;

    void triggerGlowAnimation(QPropertyAnimation* anim, bool active);
    void drawGlowEffect(QPainter& p, const QPainterPath& clipPath, qreal glowAlpha);
};

#endif // VIEWER_TOPBAR_H
