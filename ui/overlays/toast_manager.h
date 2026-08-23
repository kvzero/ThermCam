#ifndef TOAST_MANAGER_H
#define TOAST_MANAGER_H

#include <QWidget>
#include <QList>
#include "core/types.h"

/**
 * @brief Global Notification Overlay (Layer 3).
 *
 * Owns a short stack of non-blocking notifications. Each toast has an
 * independent lifetime; expanding and collapsing occupied height drives the
 * stack reflow animation.
 */
class ToastManager : public QWidget {
    Q_OBJECT

public:
    explicit ToastManager(QWidget* parent = nullptr);
    ~ToastManager() override;

    /**
     * @brief Displays a system notification in the top overlay stack.
     * @param msg The text payload.
     * @param level Determines the visual style and icon.
     */
    void showToast(const QString& msg, ToastLevel level = ToastLevel::Info);

    /** @brief Shows one pinned, non-dismissible toast with indeterminate progress. */
    void showProgressToast(const QString& msg);

    /** @brief Switches the active progress toast to determinate progress. */
    void updateProgressToast(int percent);

    /** @brief Dismisses a progress toast, then presents a standard timed result toast. */
    void finishProgressToast(const QString& msg, ToastLevel level);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct ToastEntry;

    ToastEntry* createEntry(const QString& msg, ToastLevel level, bool progress);
    void startEntry(ToastEntry* entry);
    void dismissEntry(ToastEntry* entry);
    void removeEntry(ToastEntry* entry);
    void relayoutEntries();
    ToastEntry* entryAt(const QPoint& globalPosition) const;
    void beginDrag(ToastEntry* entry, int globalY);
    void updateDrag(int globalY);
    void finishDrag();
    int activeEntryCount() const;

    QList<ToastEntry*> m_entries;
    ToastEntry* m_progressEntry = nullptr;
    ToastEntry* m_dragEntry = nullptr;
    int m_dragStartGlobalY = 0;
    qreal m_dragStartOffsetY = 0.0;
};

#endif // TOAST_MANAGER_H
