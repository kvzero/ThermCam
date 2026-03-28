#ifndef APP_H
#define APP_H

#include <QWidget>
#include <core/types.h>

class QStackedWidget;
class BaseView;

class GalleryView;
// class SettingsView;
// class QuickSettings;
class ConfirmDialog;
class ToastManager;
class TransitionLayer;

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
     */
    void switchView(ViewType type, const QRect& sourceAnchor = QRect());

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
    void showConfirmDialog(const QString& title, std::function<void()> onConfirm);
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
    // BaseView* m_settingsView = nullptr;

    /* Layer 1: Visual Transitions */
    TransitionLayer* m_transitionLayer = nullptr;

    /* Layer 2: Global System Overlays */
    // QuickSettings* m_quickSettings = nullptr;
    ConfirmDialog* m_confirmDialog = nullptr;
    ToastManager* m_toastManager = nullptr;
};

#endif // APP_H
