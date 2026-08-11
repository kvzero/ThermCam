#ifndef BASE_VIEW_H
#define BASE_VIEW_H

#include <QWidget>

/**
 * @brief The abstract contract for all full-screen views.
 *
 * BaseView is the Layer-0 scene owner in App's view stack.
 */
class BaseView : public QWidget {
    Q_OBJECT
public:
    explicit BaseView(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual ~BaseView() = default;

    /* --- Lifecycle Hooks --- */
    /** @brief Called when this view becomes the active Layer-0 scene. */
    virtual void onEnter() {}
    /** @brief Called when this view is about to leave the active scene. */
    virtual void onExit() {}

    /* --- Transition Anchor Hooks --- */
    /**
     * @brief Returns the camera entry anchor widget used by App's fallback
     * transition target resolution, or nullptr when unavailable.
     */
    virtual QWidget* capsuleWidget() { return nullptr; }

    /* --- Hardware Key Dispatcher --- */
    /**
     * @brief Must be implemented by derived classes to handle the primary physical button.
     */
    virtual void handleKeyShortPress() = 0;

    /**
     * @brief Clears temporary UI state before new high-priority interactions.
     */
    virtual void resetTransientUi() {}

};

#endif // BASE_VIEW_H
