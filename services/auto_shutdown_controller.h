#ifndef AUTO_SHUTDOWN_CONTROLLER_H
#define AUTO_SHUTDOWN_CONTROLLER_H

#include "core/settings_types.h"

#include <QObject>

class QTimer;

/**
 * @brief Owns the inactivity deadline before automatic power-off.
 *
 * The final visible countdown belongs to the UI. Once requested, this controller waits for the
 * UI to either record user activity or commit shutdown.
 */
class AutoShutdownController final : public QObject {
    Q_OBJECT
public:
    explicit AutoShutdownController(QObject* parent = nullptr);

    /** @brief Records touch or physical-key activity from the application input boundary. */
    void notifyActivity();
    /** @brief Commits automatic shutdown after the visible countdown finishes. */
    void commitShutdown();

signals:
    /** @brief Requests presentation of the final countdown. */
    void countdownRequested();
    /** @brief Signals one committed automatic power-off request. */
    void shutdownRequested();

private:
    enum class Stage {
        Disabled,
        Waiting,
        CountdownPresented,
        ShutdownCommitted
    };

    void setTimeout(AutoShutdownTimeout timeout);
    void resetDeadline();
    void onWakeup();

    QTimer* m_wakeupTimer = nullptr;
    Stage m_stage = Stage::Disabled;
    int m_timeoutMilliseconds = 0;
};

#endif // AUTO_SHUTDOWN_CONTROLLER_H
