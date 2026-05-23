#ifndef BASE_VIEW_H
#define BASE_VIEW_H

#include <QWidget>
#include <QPoint>
#include "ui/interaction_target.h"

/**
 * @brief The abstract contract for all full-screen views.
 *
 * BaseView is both:
 * - a Layer-0 scene owner in App's view stack, and
 * - an InteractionTarget fallback when no foreground widget captures the touch.
 */
class BaseView : public QWidget, public InteractionTarget {
    Q_OBJECT
    Q_INTERFACES(InteractionTarget)
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

    /* --- InteractionTarget Defaults --- */
    void onInteractionBegin(const InteractionEvent& event) override { Q_UNUSED(event); }
    InteractionUpdateDecision onInteractionUpdate(const InteractionEvent& event) override {
        Q_UNUSED(event);
        return InteractionUpdateDecision::KeepOwner;
    }
    void onInteractionEnd(const InteractionEvent& event) override { Q_UNUSED(event); }
    void onInteractionCancel() override {}

    /**
     * @brief Clears temporary UI state before new high-priority interactions.
     */
    virtual void resetTransientUi() {}

};

#endif // BASE_VIEW_H
