#ifndef UI_WIDGETS_MODE_SELECTOR_H
#define UI_WIDGETS_MODE_SELECTOR_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QPoint>
#include "core/types.h"

/**
 * @brief Professional Mode Selection and Recording Feedback Widget.
 *
 * This widget handles the dual-expansion logic:
 * 1. Vertical expansion (Picking State): Triggered by long-press for mode selection.
 * 2. Horizontal expansion (Recording State): Triggered by service for status feedback.
 *
 * Visuals follow the "Glass DNA": Frosted background, inner stroke, and adaptive contrast.
 */
class ModeSelector : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal vStretch READ vStretch WRITE setVStretch)
    Q_PROPERTY(qreal hStretch READ hStretch WRITE setHStretch)
    Q_PROPERTY(qreal iconPop READ iconPop WRITE setIconPop)
    Q_PROPERTY(qreal glowOpacity READ glowOpacity WRITE setGlowOpacity)

public:
    enum class State {
        Normal,     /**< Single icon circle */
        Picking,    /**< Vertically expanded list for mode selection */
        Recording   /**< Horizontally expanded bar for recording status */
    };

    explicit ModeSelector(QWidget* parent = nullptr);

    /** --- InteractionArbiter Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);
    Q_INVOKABLE void longPressed();
    Q_INVOKABLE void collapse();

    /** @brief Sets overall widget opacity for HUD immersion transitions. */
    void setContentsOpacity(qreal opacity) { m_contentsOpacity = opacity; update(); }

    /* Property Accessors for Animation Engine */
    qreal vStretch() const { return m_vStretch; }
    void setVStretch(qreal s);
    qreal hStretch() const { return m_hStretch; }
    void setHStretch(qreal s) { m_hStretch = s; update(); }
    qreal iconPop() const { return m_iconPop; }
    void setIconPop(qreal s) { m_iconPop = s; update(); }
    qreal glowOpacity() const { return m_glowOpacity; }
    void setGlowOpacity(qreal o) { m_glowOpacity = o; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    /* Service State Synchronization */
    void onModeChanged(CaptureMode mode);
    void onRecordingStarted();
    void onRecordingStopped();
    void onRecordingPaused(bool paused);
    void onDurationUpdated(const QString& time);
    void onBlinkTick(bool visible);

private:
    /** @brief Internal drawing helper for icons with contrast shadows. */
    void drawIcon(QPainter& p, const QRect& r, CaptureMode mode, bool active, qreal scale);

    /** @brief Internal drawing helper for the recording status bar content. */
    void drawRecordingInfo(QPainter& p, const QRect& r);

    bool isPointInVisualArea(const QPoint& pos);

    /* --- State Data --- */
    QTimer* m_longPressTimer;
    State m_state = State::Normal;
    CaptureMode m_currentMode = CaptureMode::Photo;
    CaptureMode m_pendingMode = CaptureMode::Photo; /**< Mode requested but waiting for animation halfway point */
    CaptureMode m_hoveredMode = CaptureMode::Photo; /**< Mode currently under the finger in Picking state */

    /* --- Interaction & Visual State --- */
    bool m_isPressed = false;
    qreal m_vStretch = 0.0;
    qreal m_hStretch = 0.0;
    qreal m_iconPop = 1.0;
    qreal m_glowOpacity = 0.0;
    qreal m_contentsOpacity = 1.0;
    QPoint m_glowPos;

    /* --- Recording Content --- */
    QString m_timeStr = "00:00";
    bool m_dotVisible = true;
    bool m_isPaused = false;
    bool m_isStickyPicking = false; /**< True if Picking state was just triggered by LongPress */

    /* --- Visual Configuration --- */
    struct Config {
        const QColor BG_START      = QColor(40, 40, 40, 90);
        const QColor BG_END        = QColor(20, 20, 20, 100);
        const QColor INNER_STROKE  = QColor(255, 255, 255, 45);
        const QColor GLOW_CORE     = QColor(255, 255, 255, 180);
        const QColor GLOW_HALO     = QColor(255, 255, 255, 45);
        const QColor SHADOW_COLOR  = QColor(0, 0, 0, 180);
        const QColor RECORD_RED    = QColor(255, 59, 48);

        // Icons from tabler-icons
        const QString ICON_CAM_OUT  = QChar(0xea54);
        const QString ICON_CAM_FILL = QChar(0xfa37);
        const QString ICON_VID_OUT  = QChar(0xed22);
        const uint    ICON_VID_HEX  = 0x1009b; // Requires UCS4 handling

        const int INTERNAL_LONG_PRESS_MS = 425;
    } m_cfg;

    QPropertyAnimation *m_vAnim, *m_hAnim, *m_glowAnim, *m_popAnim;
};

#endif // UI_WIDGETS_MODE_SELECTOR_H
