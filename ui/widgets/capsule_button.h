#ifndef CAPSULE_BUTTON_H
#define CAPSULE_BUTTON_H

#include <QWidget>
#include <QPoint>
#include <QColor>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>

/**
 * @brief Industrial-grade interaction widget for Settings and Gallery access.
 * Implements "Explicit Intent" protocol: Only captures touches starting inside.
 * Features dual-layer glow, hysteresis-based zone switching, and "Pop" animations.
 */
class CapsuleButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal topIconScale READ topIconScale WRITE setTopIconScale)
    Q_PROPERTY(qreal bottomIconScale READ bottomIconScale WRITE setBottomIconScale)
    Q_PROPERTY(qreal glowOpacity READ glowOpacity WRITE setGlowOpacity)

public:
    enum class ActiveZone { None, Settings, Gallery };

    explicit CapsuleButton(QWidget* parent = nullptr);

    /* --- UIController Interaction Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);
    Q_INVOKABLE void longPressed();

    /* --- Property Accessors --- */
    qreal topIconScale() const { return m_topIconScale; }
    void setTopIconScale(qreal s) { m_topIconScale = s; update(); }
    qreal bottomIconScale() const { return m_bottomIconScale; }
    void setBottomIconScale(qreal s) { m_bottomIconScale = s; update(); }
    qreal glowOpacity() const { return m_glowOpacity; }
    void setGlowOpacity(qreal o) { m_glowOpacity = o; update(); }

    /** @brief Sets the overall opacity for the entire widget. */
    void setContentsOpacity(qreal opacity) { m_contentsOpacity = opacity; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void updateZone(const QPoint& pos);
    void triggerPopAnimation(ActiveZone zone);
    void startLongPressTimer();

    /* --- Visual Constants --- */
    struct Config {
        // Aesthetics
        const QColor BG_START       = QColor(40, 40, 40, 90);
        const QColor BG_END         = QColor(20, 20, 20, 100);
        const QColor INNER_STROKE   = QColor(255, 255, 255, 45);
        const QColor GLOW_CORE      = QColor(255, 255, 255, 180);
        const QColor GLOW_HALO      = QColor(255, 255, 255, 45);
        const QColor SHADOW_COLOR   = QColor(0, 0, 0, 180);

        // Dynamics
        const int LONG_PRESS_MS     = 425;
        const int ANIM_DURATION_MS  = 180;
        const qreal HYSTERESIS_PCT  = 0.05; // 5% deadzone
        const qreal SCALE_IDLE      = 1.0;
        const qreal SCALE_POP       = 1.25;
        const qreal ICON_BASE_SIZE_RATIO = 0.55;
        const qreal ICON_SETTING_OPTICAL_SCALE = 1.08;
        const qreal ICON_GALLERY_OPTICAL_SCALE = 1.0;

        // Icons (Tabler Icons Unicode)
        const QString ICON_SETTING_OUTLINE = QChar(0xeb20);
        const QString ICON_SETTING_FILLED  = QChar(0xf69e);
        const QString ICON_GALLERY_OUTLINE = QChar(0xeb0a);
        const QString ICON_GALLERY_FILLED  = QChar(0xfa4a);
    } m_cfg;

    /* --- State Management --- */
    ActiveZone m_currentZone = ActiveZone::None;
    bool m_isInside          = false;
    QPoint m_glowPos;

    /* --- Animation & Logic --- */
    QTimer* m_longPressTimer = nullptr;
    qreal m_topIconScale     = 1.0;
    qreal m_bottomIconScale  = 1.0;
    qreal m_glowOpacity      = 0.0;
    qreal m_contentsOpacity  = 1.0;

    QPropertyAnimation* m_topAnim    = nullptr;
    QPropertyAnimation* m_bottomAnim = nullptr;
    QPropertyAnimation* m_glowAnim   = nullptr;
};

#endif // CAPSULE_BUTTON_H
