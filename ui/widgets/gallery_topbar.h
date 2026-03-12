#ifndef GALLERY_TOPBAR_H
#define GALLERY_TOPBAR_H

#include <QWidget>
#include <QPropertyAnimation>

/**
 * @brief Floating navigation and action bar for the Gallery.
 *
 * Implements a state-machine driven metaball (Gooey) morphing animation.
 * Features an "Energy Transfer" visual feedback system:
 * Touch interaction emits a white glow, which erupts into a red shockwave
 * upon release, seamlessly pulling out the Trash module.
 */
class GalleryTopBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal morphProgress READ morphProgress WRITE setMorphProgress)
    Q_PROPERTY(qreal trashExtraWidth READ trashExtraWidth WRITE setTrashExtraWidth)
    Q_PROPERTY(qreal leftGlow READ leftGlow WRITE setLeftGlow)
    Q_PROPERTY(qreal rightGlow READ rightGlow WRITE setRightGlow)
    Q_PROPERTY(qreal eruption READ eruption WRITE setEruption)
    Q_PROPERTY(qreal retractGlow READ retractGlow WRITE setRetractGlow)

public:
    explicit GalleryTopBar(QWidget* parent = nullptr);

    /* --- UIController Interaction Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);

    /* --- State Mutators --- */
    void setSelectionMode(bool active);

    /**
     * @brief Unidirectional data flow. Updates internal state and
     * triggers localized geometry expansions for the number badge.
     */
    void updateSelectionState(int selectedCount, int totalCount);

    /* --- Animation Properties --- */
    qreal morphProgress() const { return m_morphProgress; }
    void setMorphProgress(qreal p) { m_morphProgress = p; update(); }

    qreal trashExtraWidth() const { return m_trashExtraWidth; }
    void setTrashExtraWidth(qreal w) { m_trashExtraWidth = w; update(); }

    qreal leftGlow() const { return m_leftGlow; }
    void setLeftGlow(qreal g) { m_leftGlow = g; update(); }

    qreal rightGlow() const { return m_rightGlow; }
    void setRightGlow(qreal g) { m_rightGlow = g; update(); }

    qreal eruption() const { return m_eruption; }
    void setEruption(qreal e) { m_eruption = e; update(); }

    qreal retractGlow() const { return m_retractGlow; }
    void setRetractGlow(qreal r) { m_retractGlow = r; update(); }


signals:
    void backRequested();
    void selectionModeToggled(bool active);
    void selectAllClicked();
    void deleteRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void drawLeftAction(QPainter& p, int H);
    void drawRightAction(QPainter& p, int H);
    void triggerGlowAnimation(QPropertyAnimation* anim, bool active);

    struct Config {
        const qreal MARGIN_RATIO      = 0.15;
        const qreal BTN_HEIGHT_RATIO  = 0.65;

        const QColor BG_NORMAL        = QColor(40, 40, 40, 210);
        const QColor STROKE_COLOR     = QColor(255, 255, 255, 45);
        const QColor TRASH_RED        = QColor(255, 59, 48);
        const QColor GLOW_WHITE       = QColor(255, 255, 255, 80);

        const QString ICON_BACK       = QChar(0xea60);
        const QString ICON_TRASH      = QChar(0xeb41);
    } m_cfg;

    /* --- State Data --- */
    bool m_isSelectionMode = false;
    bool m_isAllSelected = false;
    int m_selectedCount = 0;
    int m_totalCount = 0;
    qreal m_retractGlow = 0.0;

    /* --- Layout & Physics Cache --- */
    qreal m_morphProgress = 0.0;
    qreal m_trashExtraWidth = 0.0;
    qreal m_targetExtraWidth = 0.0; // Used to calculate proportional pop scale

    QRectF m_leftHitbox;
    QRectF m_rightCancelHitbox;
    QRectF m_rightTrashHitbox;

    /* --- Visual Feedback State --- */
    QPointF m_lastGlowPos;
    QPointF m_eruptionOrigin;
    bool m_hoverLeft = false;
    bool m_hoverRightCancel = false;
    bool m_hoverTrash = false;

    qreal m_leftGlow = 0.0;
    qreal m_rightGlow = 0.0;
    qreal m_eruption = 0.0;

    /* --- Animators --- */
    QPropertyAnimation* m_morphAnim = nullptr;
    QPropertyAnimation* m_widthAnim = nullptr;
    QPropertyAnimation* m_leftGlowAnim = nullptr;
    QPropertyAnimation* m_rightGlowAnim = nullptr;
    QPropertyAnimation* m_eruptionAnim = nullptr;
    QPropertyAnimation* m_retractGlowAnim = nullptr;
};

#endif // GALLERY_TOPBAR_H
