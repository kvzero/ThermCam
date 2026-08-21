#ifndef POWEROFF_OVERLAY_H
#define POWEROFF_OVERLAY_H

#include <QWidget>
#include <QString>

class QPropertyAnimation;
class QTimer;
class QVariantAnimation;
class SystemControl;

/**
 * @brief Full-screen, input-blocking owner of one irreversible power-off flow.
 *
 * It owns the short pre-power-off presentation and runtime-only backlight fade.
 * The injected SystemControl is used only for immediate hardware output; this
 * flow never changes the persisted brightness preference.
 */
class PoweroffOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal spinnerPhase READ spinnerPhase WRITE setSpinnerPhase)

public:
    enum class Reason {
        UserRequested,
        AutoShutdown,
        BatteryDepleted
    };

    explicit PoweroffOverlay(SystemControl* systemControl, QWidget* parent = nullptr);
    ~PoweroffOverlay() override = default;

    /**
     * @brief Starts the non-cancellable shutdown flow for one reason.
     *
     * Duplicate requests are ignored while the overlay is visible so one flow
     * retains one visual deadline.
     */
    void start(Reason reason);

    qreal spinnerPhase() const { return m_spinnerPhase; }
    void setSpinnerPhase(qreal phase);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setReason(Reason reason);

    qreal m_spinnerPhase = 0.0;
    QString m_message;
    QPropertyAnimation* m_spinnerAnimation = nullptr;
    QTimer* m_backlightFadeTimer = nullptr;
    QVariantAnimation* m_backlightFade = nullptr;
    SystemControl* m_systemControl = nullptr;
    int m_originalBrightnessPercent = 100;

    static constexpr int kSpinnerCycleMs = 900;
    static constexpr int kBacklightFadeStartMs = 1000;
    static constexpr int kBacklightFadeDurationMs = 500;
};

#endif // POWEROFF_OVERLAY_H
