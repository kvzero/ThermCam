#ifndef APP_H
#define APP_H

#include <QWidget>
#include <QDateTime>
#include <optional>
#include <core/types.h>
#include "ui/overlays/modal_dialog.h"
#include "ui/settings_catalog.h"

class QStackedWidget;
class BaseView;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QShowEvent;

class GalleryView;
class SettingsView;
// class QuickSettings;
class ToastManager;
class TransitionLayer;
class TextModal;
class ClockModal;

/**
 * @brief The Root UI Container (Body).
 *
 * Manages the physical layout, Z-order layering, and view lifecycle.
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

    /* --- Accessors for System Overlays --- */
    // QuickSettings* quickSettings() const;
    void showTextModal(const QString& title,
                       std::function<void()> onPrimaryAction,
                       ModalLevel level = ModalLevel::Critical,
                       TextModalSize size = TextModalSize::Normal);
    void showClockModal(std::function<bool(const QDateTime&, QString*)> onCommit);
    void showToast(const QString& message, ToastLevel level);

protected:
    /**
     * @brief Manually manages Z-order layout.
     * Overlays are resized to cover specific areas or the full screen.
     */
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void initLayer_Stack();
    void initLayer_Overlays();
    void connectHardwareKeys();
    void openSettingsItem(SettingID item,
                          const QRect& sourceAnchor,
                          TransitionMode transitionMode);
    void activateView(ViewType type, BaseView* previousView);
    void handleHardwareKeyPressed();
    void handleHardwareKeyShortPress();
    void handleHardwareKeyLongPress();

    /* Layer 0: Business Views */
    QStackedWidget* m_viewStack = nullptr;
    BaseView* m_cameraView = nullptr;
    BaseView* m_galleryView = nullptr;
    SettingsView* m_settingsView = nullptr;
    std::optional<SettingID> m_pendingSettingsItem;

    /* Layer 1: Visual Transitions */
    TransitionLayer* m_transitionLayer = nullptr;

    /* Layer 2: Global System Overlays */
    // QuickSettings* m_quickSettings = nullptr;
    TextModal* m_textModal = nullptr;
    ClockModal* m_clockModal = nullptr;
    ToastManager* m_toastManager = nullptr;

    /* Startup black curtain: fades away once the fullscreen window is visible. */
    QWidget* m_startupMask = nullptr;
    QGraphicsOpacityEffect* m_startupMaskOpacity = nullptr;
    QPropertyAnimation* m_startupMaskFade = nullptr;
    bool m_startupMaskFadeStarted = false;
};

#endif // APP_H
