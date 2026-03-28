#ifndef CONFIRM_DIALOG_H
#define CONFIRM_DIALOG_H

#include <QWidget>
#include <QPoint>
#include <QPropertyAnimation>
#include <functional>

class DialogButton : public QWidget {
    Q_OBJECT
public:
    explicit DialogButton(const QString& text, bool isDestructive, QWidget* parent = nullptr);

    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);

    bool isPressed() const { return m_isPressed; }
    bool isDestructive() const { return m_isDestructive; }
    QString text() const { return m_text; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QString m_text;
    bool m_isDestructive;
    bool m_isPressed = false;
};

class ConfirmDialog : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animProgress READ animProgress WRITE setAnimProgress)
    Q_PROPERTY(qreal touchProgress READ touchProgress WRITE setTouchProgress)

public:
    explicit ConfirmDialog(QWidget* parent = nullptr);

    void showMessage(const QString& title, std::function<void()> onConfirm);
    void dismiss();

    /** --- InteractionArbiter Interaction Protocol --- */
    Q_INVOKABLE bool handleInteractionUpdate(QPoint localPos);
    Q_INVOKABLE void finalizeGesture(int dy);

    /** --- Internal Feedback Interface --- */
    void setInteractionActive(bool active, const QPoint& localPos = QPoint());

    qreal animProgress() const { return m_animProgress; }
    void setAnimProgress(qreal p) { m_animProgress = p; update(); }
    qreal touchProgress() const { return m_touchProgress; }
    void setTouchProgress(qreal p) { m_touchProgress = p; update(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawButtonSkin(QPainter& p, DialogButton* btn);

    struct Config {
        const qreal BOX_W_RATIO       = 0.65;
        const qreal BOX_H_RATIO       = 0.55;
        const qreal BOX_CORNER_RADIUS = 42.0;
        const qreal BTN_W_RATIO       = 0.42;
        const qreal BTN_H_RATIO       = 0.22;

        const qreal SCALE_POP_START   = 0.80;
        const qreal SCALE_TOUCH_MAX   = 1.03;

        const QColor MASK_COLOR       = QColor(0, 0, 0, 180);
        const QColor BOX_BG_START     = QColor(35, 35, 35, 215);
        const QColor BOX_BG_END       = QColor(20, 20, 20, 235);
        const QColor BOX_STROKE       = QColor(255, 255, 255, 55);
        const QColor GLOW_COLOR       = QColor(255, 255, 255, 45);

        const int DURATION_POP_MS     = 250;
        const int DURATION_EXIT_MS    = 125;
        const int DURATION_TOUCH_MS   = 150;
    } m_cfg;

    QRect m_boxRect;
    QString m_title;
    std::function<void()> m_confirmCallback;

    QPoint m_glowPos;
    bool m_isBoxPressed = false;

    qreal m_animProgress = 0.0;
    qreal m_touchProgress = 0.0;
    QPropertyAnimation* m_popAnim = nullptr;
    QPropertyAnimation* m_touchAnim = nullptr;

    DialogButton* m_btnOk = nullptr;
    DialogButton* m_btnCancel = nullptr;
};

#endif // CONFIRM_DIALOG_H
