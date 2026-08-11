#ifndef MODAL_DIALOG_H
#define MODAL_DIALOG_H

#include <QWidget>
#include <QPoint>
#include <QPropertyAnimation>
#include <functional>

class QMouseEvent;
class QPainter;

enum class ModalLevel {
    Normal,
    Critical
};

struct ModalSpec {
    ModalLevel level = ModalLevel::Critical;
    QString primaryText = "CONFIRM";
    QString secondaryText = "CANCEL";
    bool dismissOnMaskTap = true;
    std::function<void()> onPrimaryAction;
    std::function<void()> onSecondaryAction;
};

/**
 * @brief Layer-2 modal dialog base owned by App overlay stack.
 *
 * ModalBase is the single owner of modal chrome, entry/exit animation,
 * shell press feedback, and primary/secondary action contract dispatch. Subclasses
 * only provide content sizing/painting and optional content interaction.
 */
class ModalBase : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animProgress READ animProgress WRITE setAnimProgress)
    Q_PROPERTY(qreal touchProgress READ touchProgress WRITE setTouchProgress)

public:
    struct ContentLayout {
        QSize preferred;
        qreal heightRatio = 0.55;
    };

    /* --- Lifecycle --- */
    explicit ModalBase(QWidget* parent = nullptr);
    ~ModalBase() override = default;

    /* --- Public API --- */
    void present(const ModalSpec& spec);
    void dismiss();

    /* --- Animation Properties --- */
    qreal animProgress() const { return m_animProgress; }
    void setAnimProgress(qreal p) { m_animProgress = p; update(); }
    qreal touchProgress() const { return m_touchProgress; }
    void setTouchProgress(qreal p) { m_touchProgress = p; update(); }

protected:
    /* --- Content Extension Points --- */
    virtual ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                            const QSize& viewportSize) const = 0;
    virtual void paintContent(QPainter& p, const QRect& contentRect) = 0;
    virtual bool onPrimaryAction();
    virtual bool onSecondaryAction();
    virtual bool contentPress(const QPoint& contentPos);
    virtual bool contentMove(const QPoint& contentPos);
    virtual bool contentRelease(const QPoint& contentPos);
    virtual void contentCancel();

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /* --- Interaction Model --- */
    enum class PressTarget {
        None,
        Panel,
        SecondaryButton,
        PrimaryButton,
        Content
    };

    /* --- Layout Helpers --- */
    void relayout();
    PressTarget zoneAt(const QPoint& pos) const;

    /* --- Gesture Session Helpers --- */
    void beginPress(const QPoint& localPos);
    void updatePress(const QPoint& localPos);
    void endPress(bool allowAction);
    void clearPressState();
    void setInteractionActive(bool active, const QPoint& localPos = QPoint());

    /* --- Action Dispatch --- */
    bool tryPrimaryAction();
    bool trySecondaryAction();
    QColor primaryButtonColor() const;
    void onPopAnimFinished();

    /* --- Visual Config --- */
    struct Config {
        const qreal BASE_W_RATIO = 0.65;
        const qreal BASE_H_RATIO = 0.55;
        const qreal MAX_W_RATIO = 0.90;
        const qreal MAX_H_RATIO = 0.86;
        const int CONTENT_PAD_X = 30;
        const qreal CONTENT_H_RATIO = 0.55;
        const qreal BTN_W_RATIO = 0.42;
        const qreal BTN_H_RATIO = 0.22;
        const qreal BTN_BOTTOM_MARGIN_RATIO = 0.60;
        const qreal BOX_CORNER_RADIUS = 42.0;

        const qreal SCALE_POP_START = 0.80;
        const qreal SCALE_TOUCH_MAX = 1.03;

        const QColor MASK_COLOR = QColor(0, 0, 0, 180);
        const QColor BOX_BG_START = QColor(35, 35, 35, 215);
        const QColor BOX_BG_END = QColor(20, 20, 20, 235);
        const QColor BOX_STROKE = QColor(255, 255, 255, 55);
        const QColor GLOW_COLOR = QColor(255, 255, 255, 45);
        const QColor BTN_NEUTRAL = QColor(60, 60, 60);
        const QColor BTN_CRITICAL = QColor(190, 30, 30);
        const QColor BTN_TEXT = Qt::white;

        const int DURATION_POP_MS = 250;
        const int DURATION_EXIT_MS = 125;
        const int DURATION_TOUCH_MS = 160;
        const int MASK_TAP_MAX_DISTANCE_PX = 10;
    } m_cfg;

    /* --- Runtime State --- */
    ModalSpec m_spec;
    QRect m_panelRect;
    QRect m_contentRect;
    QRect m_secondaryRect;
    QRect m_primaryRect;

    PressTarget m_pressStartTarget = PressTarget::None;
    PressTarget m_currentTarget = PressTarget::None;
    QPoint m_pressStartPos;
    QPoint m_lastPos;
    QPoint m_glowPos;
    bool m_isPanelPressed = false;
    bool m_isDismissing = false;

    /* --- Animation Engine --- */
    qreal m_animProgress = 0.0;
    qreal m_touchProgress = 0.0;
    QPropertyAnimation* m_popAnim = nullptr;
    QPropertyAnimation* m_touchAnim = nullptr;
};

/* --- Text Modal Dialog --- */

/**
 * @brief Text-only modal payload built on ModalBase contract.
 *
 * Used by current poweroff/delete confirmation flows. It preserves the
 * historical visual rhythm while inheriting shared shell animation logic.
 */
class TextModal : public ModalBase {
    Q_OBJECT
public:
    /* --- Lifecycle --- */
    explicit TextModal(QWidget* parent = nullptr);
    ~TextModal() override = default;

    /* --- Public API --- */
    void setMessage(const QString& message);

protected:
    ContentLayout contentLayoutHint(const QSize& maxContentSize,
                                    const QSize& viewportSize) const override;
    void paintContent(QPainter& p, const QRect& contentRect) override;

private:
    /* --- Runtime State --- */
    QString m_message;
};

#endif // MODAL_DIALOG_H
