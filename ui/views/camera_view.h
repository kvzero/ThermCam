#ifndef CAMERA_VIEW_H
#define CAMERA_VIEW_H

#include "ui/views/base_view.h"
#include "ui/widgets/thermal_marker.h"
#include "core/types.h"
#include "processing/thermal_palette.h"
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QTimer>

class ThermalProcessor;
class StatusBar;
class CapsuleButton;
class ModeSelector;
class PaletteSelector;

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

    /* --- InteractionTarget Contract --- */
    void onInteractionBegin(const InteractionEvent& event) override;
    InteractionUpdateDecision onInteractionUpdate(const InteractionEvent& event) override;
    void onInteractionEnd(const InteractionEvent& event) override;
    void onInteractionCancel() override;
    void onInteractionTap(const InteractionEvent& event) override;
    void onInteractionLongPress(const InteractionEvent& event) override;

    /* --- Transition Anchor Hook --- */
    /** @brief Exposes the capsule widget as Camera's fallback transition anchor. */
    QWidget* capsuleWidget() override;

    qreal shutterProgress() const { return m_shutterProgress; }
    void setShutterProgress(qreal p) { m_shutterProgress = p; update(); }
    void setHudVisible(bool visible, bool animated = true);
    bool isHudVisible() const { return m_hudVisible; }

public slots:
    void updateFrame(const VisualFrame& frame);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyPalette(ThermalPalette::Id palette, bool emitHaptic);
    void connectHardware();
    void disconnectHardware();
    void updateHudLayout();
    void applyHudState(bool visible);
    void stopHudAnimations();
    void openPaletteSelector();
    void closePaletteSelector(bool commitSelection);
    void refreshPalettePreviews();

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
    bool m_isFahrenheit = false;

    /* Layer 2: Interactive HUD (Widgets owned directly by CameraView) */
    StatusBar* m_statusBar = nullptr;
    CapsuleButton* m_capsuleButton = nullptr;
    ModeSelector* m_modeSelector = nullptr;
    QParallelAnimationGroup* m_hudAnimGroup = nullptr;
    QPropertyAnimation* m_statusBarAnim = nullptr;
    QPropertyAnimation* m_capsuleAnim = nullptr;
    QPropertyAnimation* m_modeSelectorAnim = nullptr;
    PaletteSelector* m_paletteSelector = nullptr;
    QTimer* m_paletteOpenTimer = nullptr;
    QPoint m_statusBarVisiblePos;
    QPoint m_statusBarHiddenPos;
    QPoint m_capsuleVisiblePos;
    QPoint m_capsuleHiddenPos;
    QPoint m_modeSelectorVisiblePos;
    QPoint m_modeSelectorHiddenPos;
    bool m_hudVisible = true;
    ThermalPalette::Id m_currentPalette = ThermalPalette::Id::Spectra;
    ThermalPalette::Id m_lastHapticPalette = ThermalPalette::Id::Count;

    /* Layer 3: Zero-Widget Shutter Animation Engine */
    qreal m_shutterProgress = 0.0;
    QPropertyAnimation* m_shutterAnim = nullptr;

    struct HudConfig {
        static constexpr qreal STATUS_BAR_H_RATIO = 0.118;
        static constexpr qreal CAPSULE_W_RATIO = 0.15;
        static constexpr qreal CAPSULE_H_RATIO = 0.40;
        static constexpr qreal CAPSULE_MARGIN_RATIO = 0.03;
        static constexpr qreal MODE_W_RATIO = 0.33;
        static constexpr qreal MODE_H_RATIO = 0.40;
        static constexpr qreal BOTTOM_TRIGGER_ZONE_RATIO = 0.16;
        static constexpr int SWIPE_DEADZONE_PX = 10;
        static constexpr int SWIPE_HIDE_THRESHOLD_PX = 72;
        static constexpr int SWIPE_SHOW_THRESHOLD_PX = 72;
        static constexpr int PALETTE_SHOW_THRESHOLD_PX = 56;
        static constexpr int PALETTE_SHOW_DELAY_MS = 90;
        static constexpr int EDGE_CONFIRM_MARGIN_PX = 2;
        static constexpr int ANIM_DURATION_MS = 260;
    } m_hudCfg;

    enum class SwipeAxis { None, Horizontal, Vertical };
    SwipeAxis m_swipeAxis = SwipeAxis::None;
};


#endif // CAMERA_VIEW_H
