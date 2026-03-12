#ifndef VIDEO_CONTROLBAR_H
#define VIDEO_CONTROLBAR_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
#include <QRectF>
#include <QPoint>
#include <QColor>
#include <QString>

class QPainter;

/**
 * @brief High-precision Video Control Bar.
 *
 * DESIGN PRINCIPLE: All visual metrics are ratio-based to ensure perfect scaling
 * across different screen resolutions. No magic numbers are allowed in the logic.
 */
class VideoControlBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
public:
    explicit VideoControlBar(QWidget* parent = nullptr);

    void show();
    void hideImmediate();

    void updatePlaybackState(bool playing, qint64 currentMs, qint64 totalMs, const QString& durationStr);

    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy = 0);

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal o) { m_opacity = o; update(); }

signals:
    void togglePlayRequested();
    void seekRequested(qint64 targetMs);
    void scrubbingStateChanged(bool isScrubbing);
    void interactionActive();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void initiateFadeOut();

private:
    QString formatTimeMs(qint64 ms, bool forceHours) const;

    struct Config {
        /* --- Layer Geometry --- */
        const qreal BAR_HEIGHT_RATIO      = 0.25;  // Height of the control bar relative to viewer
        const qreal SIDE_MARGIN_RATIO     = 0.20;  // Left/Right padding from screen edges
        const qreal ITEM_SPACING_RATIO    = 0.20;  // Horizontal gap between button and scrubber group
        const qreal CONTENT_V_START_RATIO = 0.38;  // Y-offset for the right content group

        /* --- Play Button --- */
        const qreal PLAY_ICON_SIZE_RATIO  = 0.45;
        const qreal PLAY_BTN_V_OFFSET     = 0.10;  // Vertical micro-adjustment for the button

        /* --- Scrubber: Text Tier --- */
        const qreal TEXT_TIER_H_RATIO     = 0.25;
        const qreal TIME_FONT_SCALE       = 0.85;

        /* --- Scrubber: Track Tier --- */
        const qreal TRACK_HEIGHT_RATIO    = 0.08;
        const qreal TRACK_V_GAP_RATIO     = 0.06;  // Gap between time text and progress bar
        const qreal SCRUB_HANDLE_RATIO    = 2.50;  // Size of the drag handle relative to track height

        /* --- Interaction --- */
        const qreal HIT_EXPANSION_PX      = 20.0;  // Invisible touch padding

        /* --- Aesthetics --- */
        const QColor BG_GRADIENT_END      = QColor(0, 0, 0, 220);
        const QColor TRACK_BG_COLOR       = QColor(255, 255, 255, 60);
        const QColor TEXT_COLOR           = Qt::white;
        const QString ICON_PLAY           = QChar(0xf691);
        const QString ICON_PAUSE          = QChar(0xf690);

        const int FADE_DELAY_MS           = 3000;
        const int FADE_DURATION_MS        = 250;
    } m_cfg;

    bool m_isPlaying = false;
    qint64 m_currentMs = 0;
    qint64 m_totalMs = 0;
    QString m_durationStr;

    QRectF m_playHitbox;
    QRectF m_trackHitbox;
    QRectF m_scrubHitbox;

    bool m_isScrubbing = false;
    qint64 m_scrubTargetMs = -1;

    QTimer* m_hideTimer = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;
    qreal m_opacity = 0.0;
    bool m_hoverPlay = false;
};

#endif // VIDEO_CONTROLBAR_H
