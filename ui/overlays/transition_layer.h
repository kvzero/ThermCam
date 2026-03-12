#ifndef TRANSITION_LAYER_H
#define TRANSITION_LAYER_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QImage>
#include <functional>

/**
 * @brief Global Shared-Element Transition Overlay.
 *
 * Orchestrates seamless view switching using geometric morphing and
 * view-port snapshots. It prevents visual jarring by maintaining
 * content continuity during heavy background loading or view-stack swaps.
 */
class TransitionLayer : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)
    Q_PROPERTY(qreal layerOpacity READ layerOpacity WRITE setLayerOpacity)

public:
    explicit TransitionLayer(QWidget* parent = nullptr);

    /**
     * @brief Initiates a geometric morphing sequence.
     * @param start Initial physical geometry.
     * @param end Target physical geometry.
     * @param expanding Direction flag (True: Button->Full, False: Full->Button).
     * @param icon Symbolic icon for the target view.
     * @param color Background color to match the target view.
     * @param snapshot Optional capture of the outgoing view content.
     * @param onComplete Lifecycle callback executed when the morph phase ends.
     */
    void startMorph(const QRect& start, const QRect& end, bool expanding,
                    const QString& icon, const QColor& color,
                    const QImage& snapshot, std::function<void()> onComplete);

    /**
     * @brief Initiates a full-layer alpha fade-out.
     * Usually called after 'expanding' morph to reveal the newly loaded view.
     */
    void startFadeOut(std::function<void()> onComplete);

    /* Animation Property Accessors */
    qreal progress() const { return m_progress; }
    void setProgress(qreal p) { m_progress = p; update(); }

    qreal layerOpacity() const { return m_layerOpacity; }
    void setLayerOpacity(qreal o) { m_layerOpacity = o; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /* Internal State */
    qreal  m_progress = 0.0;
    qreal  m_layerOpacity = 1.0;
    bool   m_isExpanding = true;
    QColor m_activeColor;
    QImage m_snapshot;

    QRectF  m_startRect;
    QRectF  m_endRect;
    QString m_icon;

    std::function<void()> m_morphCallback;
    std::function<void()> m_fadeCallback;

    QPropertyAnimation* m_animMorph = nullptr;
    QPropertyAnimation* m_animFade = nullptr;

    /**
     * @brief Visual and physical behavior constants.
     */
    struct Config {
        /* Animation Timing */
        const int EXPAND_DURATION        = 220;
        const int CONTRACT_DURATION      = 420;
        const int FADE_DURATION          = 240;

        /* Expansion Dynamics: Alpha ramp-up speed to hide background loading */
        const qreal EXPANSION_ALPHA_RAMP = 5.0;

        /* Contraction Logic: When to swap snapshot for the icon (scale factor) */
        const qreal SNAPSHOT_SWAP_SCALE  = 2.5;
        const qreal SNAPSHOT_FADE_START  = 0.4;

        /* Icon Aesthetics */
        const qreal ICON_GROW_FACTOR     = 0.5;
        const qreal ICON_SHRINK_START    = 1.5;
    } m_cfg;
};

#endif // TRANSITION_LAYER_H
