#include "auto_shutdown_controller.h"

#include "core/settings_store.h"

#include <QTimer>

AutoShutdownController::AutoShutdownController(QObject* parent)
    : QObject(parent),
      m_wakeupTimer(new QTimer(this)) {
    m_wakeupTimer->setSingleShot(true);
    connect(m_wakeupTimer, &QTimer::timeout, this, &AutoShutdownController::onWakeup);

    connect(&SettingsStore::instance(), &SettingsStore::settingsChanged,
            this, [this](const SettingsChangeEvent& change) {
                if (!change.changedKeys.contains(SettingKey::AutoShutdownTimeout)) return;
                setTimeout(static_cast<AutoShutdownTimeout>(
                    change.snapshot.values.value(SettingKey::AutoShutdownTimeout).toInt()));
            });

    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    setTimeout(static_cast<AutoShutdownTimeout>(
        snapshot.values.value(SettingKey::AutoShutdownTimeout).toInt()));
}

void AutoShutdownController::notifyActivity() {
    if (m_stage == Stage::ShutdownCommitted) return;
    resetDeadline();
}

void AutoShutdownController::commitShutdown() {
    if (m_stage != Stage::CountdownPresented) return;

    m_stage = Stage::ShutdownCommitted;
    emit shutdownRequested();
}

void AutoShutdownController::setTimeout(AutoShutdownTimeout timeout) {
    if (m_stage == Stage::ShutdownCommitted) return;

    m_timeoutMilliseconds = autoShutdownTimeoutMinutes(timeout) * 60 * 1000;
    if (m_stage == Stage::CountdownPresented) return;
    resetDeadline();
}

void AutoShutdownController::resetDeadline() {
    m_wakeupTimer->stop();

    if (m_timeoutMilliseconds <= 0) {
        m_stage = Stage::Disabled;
        return;
    }

    m_stage = Stage::Waiting;
    m_wakeupTimer->start(m_timeoutMilliseconds);
}

void AutoShutdownController::onWakeup() {
    if (m_stage != Stage::Waiting) return;

    m_stage = Stage::CountdownPresented;
    emit countdownRequested();
}
