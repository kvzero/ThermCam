#ifndef APP_H
#define APP_H

#include <QWidget>
#include <core/types.h>
#include "ui/overlays/modal_dialog.h"
#include "ui/overlays/bubble_dialog.h"

class QStackedWidget;
class BaseView;

class GalleryView;
class SettingsView;
// class QuickSettings;
class ToastManager;
class TransitionLayer;
class TextModal;

/**
 * @brief The Root UI Container (Body).
 *
 * Manages the physical layout, Z-order layering, and view lifecycle.
 * It provides the "stage" for InteractionArbiter to orchestrate logic.
 *
 * Layer Structure:
 * - Layer 0: View Stack (Camera, Gallery, Settings)
 * - Layer 1: Transition Overlay (Temporary animation layer)
 * - Layer 2: System Overlays (QuickSettings, Dialogs, Toasts)
 */
class App : public QWidget {
    Q_OBJECT
public:
    explicit App(QWidget *parent = nullptr);
    ~App();

    enum ViewType {
        View_Camera = 0,
        View_Gallery,
        View_Settings
    };

    /**
     * @brief Switches the active full-screen view.
     * Handles lifecycle hooks (onExit -> onEnter) and transition animations.
     * @param type The target view to switch to.
     * @param sourceAnchor Optional screen geometry (e.g., capsule button) to start the transition animation from.
     * @param transitionMode Route-level transition policy for this switch.
     */
    void switchView(ViewType type,
                    const QRect& sourceAnchor,
                    TransitionMode transitionMode);

    /**
     * @brief Returns the currently active business view (Layer 0).
     */
    BaseView* activeView() const;

    /**
     * @brief Locate a widget at global coordinates.
     * Used by InteractionArbiter for touch routing and hit testing.
     */
    QWidget* findWidgetAt(const QPoint& globalPos);

    /* --- Accessors for System Overlays --- */
    // QuickSettings* quickSettings() const;
    void showTextModal(const QString& title,
                       std::function<void()> onPrimaryAction,
                           ModalLevel level = ModalLevel::Critical);
    void showRadioListBubble(const RadioListBubble::Spec& spec,
                             const BubbleAnchorContext& anchor);
    void showSliderBubble(const SliderBubble::Spec& spec,
                          const BubbleAnchorContext& anchor);
    void dismissBubble();
    void showToast(const QString& message, ToastLevel level);

protected:
    /**
     * @brief Manually manages Z-order layout.
     * Overlays are resized to cover specific areas or the full screen.
     */
    void resizeEvent(QResizeEvent* event) override;

private:
    void initLayer_Stack();
    void initLayer_Overlays();

    /* Layer 0: Business Views */
    QStackedWidget* m_viewStack = nullptr;
    BaseView* m_cameraView = nullptr;
    BaseView* m_galleryView = nullptr;
    BaseView* m_settingsView = nullptr;

    /* Layer 1: Visual Transitions */
    TransitionLayer* m_transitionLayer = nullptr;

    /* Layer 2: Global System Overlays */
    // QuickSettings* m_quickSettings = nullptr;
    TextModal* m_textModal = nullptr;
    RadioListBubble* m_radioListBubble = nullptr;
    SliderBubble* m_sliderBubble = nullptr;
    ToastManager* m_toastManager = nullptr;
};

#endif // APP_H
