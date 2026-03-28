#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include "ui/views/base_view.h"
#include "ui/widgets/thermal_marker.h"
#include "core/types.h"
#include <QPropertyAnimation>

class ThermalProcessor;
class HudContainer;

/**
 * @brief The Main View (Layer 0 Content + Layer 1 HUD).
 *
 * Refactored to act as a passive executor for InteractionArbiter commands.
 */
class CameraView : public BaseView {
    Q_OBJECT
    Q_PROPERTY(qreal shutterProgress READ shutterProgress WRITE setShutterProgress)

public:
    explicit CameraView(QWidget* parent = nullptr);
    ~CameraView() override;

    /* --- Lifecycle (Resource Management) --- */
    void onEnter() override;
    void onExit() override;

    /* --- Hardware Actions --- */
    void handleKeyShortPress() override;
    void resetTransientUi() override;

    /* --- Semantic Gestures (From InteractionArbiter) --- */
    void onGestureStarted() override;
    void onGestureUpdate(const QPoint& /*start*/, int dx, int dy) override;
    void onGestureFinished(const QPoint& /*start*/, int dx, int dy, float vx, float vy) override;
    void onLongPressDetected(const QPoint& start) override;

    /* --- Widget Discovery (For InteractionArbiter Hit-Testing) --- */
    // Allows InteractionArbiter to know if the user tapped the capsule or mode button
    QWidget* capsuleWidget() override;
    QWidget* modeSelectorWidget() override;

    qreal shutterProgress() const { return m_shutterProgress; }
    void setShutterProgress(qreal p) { m_shutterProgress = p; update(); }

public slots:
    void updateFrame(const VisualFrame& frame);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void connectHardware();
    void disconnectHardware();

    /* --- Visual Constants for Shutter Feedback --- */
    struct ShutterConfig {
        const int   ANIM_DURATION_MS   = 800;
        const int   MAX_FILL_ALPHA     = 50;    // Base screen exposure (0-255)
        const int   MAX_STROKE_ALPHA   = 90;    // Viewfinder border brightness (0-255)
        const int   MAX_STROKE_WIDTH   = 30;    // Maximum border thickness in pixels
    } m_cfg;

    /* Logic & Data */
    ThermalProcessor* m_processor = nullptr;
    VisualFrame m_currentFrame;

    /* Layer 1: Passive Markers (Painted directly) */
    ThermalMarker m_hotMarker{ThermalMarker::Hot};
    ThermalMarker m_coldMarker{ThermalMarker::Cold};
    ThermalMarker m_centerMarker{ThermalMarker::Center};

    /* Layer 2: Interactive HUD (Widget) */
    HudContainer* m_hudContainer = nullptr;

    /* Layer 3: Zero-Widget Shutter Animation Engine */
    qreal m_shutterProgress = 0.0;
    QPropertyAnimation* m_shutterAnim = nullptr;

    enum class SwipeAxis { None, Horizontal, Vertical };
    SwipeAxis m_swipeAxis = SwipeAxis::None;
};


#endif // CAMERA_VIEW_H
