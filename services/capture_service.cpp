#include "capture_service.h"
#include "core/settings_store.h"
#include "services/capture_worker.h"
#include "services/settings_service.h"
#include "hardware/storage/storage_manager.h"
#include <QDebug>

namespace {
bool isFahrenheitFromSnapshot(const SettingsSnapshot& snapshot) {
    bool ok = false;
    const int raw = snapshot.values
                        .value(SettingKey::TemperatureUnit,
                               QVariant::fromValue(static_cast<int>(TemperatureUnit::Celsius)))
                        .toInt(&ok);
    if (!ok) return false;
    return raw == static_cast<int>(TemperatureUnit::Fahrenheit);
}

bool boolSettingFromSnapshot(const SettingsSnapshot& snapshot,
                             SettingKey key,
                             bool fallback) {
    const QVariant defaultValue = QVariant(fallback);
    const QVariant value = snapshot.values.value(key, defaultValue);
    if (!value.canConvert<bool>()) return fallback;
    return value.toBool();
}
}

CaptureService& CaptureService::instance() {
    static CaptureService inst;
    return inst;
}

CaptureService::CaptureService(QObject *parent) : QObject(parent) {
    m_blinkTimer = new QTimer(this);

    connect(m_blinkTimer, &QTimer::timeout, [this](){

        m_blinkState = !m_blinkState;
        emit blinkTick(m_blinkState);

        if (m_blinkState) updateDuration();
    });

    /* Establish off-thread worker environment */
    m_workerThread = new QThread(this);
    m_worker = new CaptureWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &CaptureWorker::frameProcessed,
            this, &CaptureService::onWorkerFrameProcessed);
    connect(m_worker, &CaptureWorker::photoSaved,
            this, &CaptureService::onPhotoSaved);
    connect(m_worker, &CaptureWorker::videoStopped,
            this, &CaptureService::onVideoStopped);
    connect(&SettingsService::instance(), &SettingsService::unitChanged,
            m_worker, &CaptureWorker::setTemperatureUnitFahrenheit);
    connect(&SettingsService::instance(), &SettingsService::saveMarkerChanged,
            m_worker, &CaptureWorker::setSaveMarkerInMediaEnabled);
    connect(&StorageManager::instance(), &StorageManager::storageSpaceCritical,
            this, &CaptureService::onStorageSpaceCritical);

    m_workerThread->start();

    const SettingsSnapshot snapshot = SettingsStore::instance().current();
    const bool initialIsFahrenheit = isFahrenheitFromSnapshot(snapshot);
    const bool initialSaveMarkerInMedia = boolSettingFromSnapshot(
        snapshot, SettingKey::SaveMarkerInMedia, true);

    QMetaObject::invokeMethod(m_worker, "setTemperatureUnitFahrenheit",
                              Qt::QueuedConnection, Q_ARG(bool, initialIsFahrenheit));
    QMetaObject::invokeMethod(m_worker, "setSaveMarkerInMediaEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, initialSaveMarkerInMedia));
}

CaptureService::~CaptureService() {
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }

    if (m_worker) {
        delete m_worker;
        m_worker = nullptr;
    }
    if (m_workerThread) {
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

void CaptureService::updateDuration() {
    qint64 totalMs = m_accumulatedMs;
    if (m_state == RecordingState::Recording) {
        totalMs += m_elapsed.elapsed();
    }

    int secs = (totalMs / 1000) % 60;
    int mins = (totalMs / 60000);
    emit durationUpdated(QString("%1:%2")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0')));
}

void CaptureService::setMode(CaptureMode mode) {
    // Mode switching is strictly prohibited during active recording sessions
    if (m_state != RecordingState::Idle) return;

    if (m_mode != mode) {
        m_mode = mode;
        emit modeChanged(m_mode);
        qInfo() << "[Capture] Mode switched to:" << (mode == CaptureMode::Photo ? "PHOTO" : "VIDEO");
    }
}

void CaptureService::handlePhysicalTrigger() {
    if (m_mode == CaptureMode::Photo) {
        doPhotoCapture();
    } else {
        // Physical key acts as a Master Start/Stop switch (no pause)
        if (m_state == RecordingState::Idle) {
            startRecording();
        } else if (m_state == RecordingState::Recording ||
                   m_state == RecordingState::Paused) {
            stopRecording();
        }
    }
}

void CaptureService::togglePause() {
    if (m_mode != CaptureMode::Video) return;

    if (m_state == RecordingState::Recording) {
        // Suspend: Save current progress to accumulator
        m_accumulatedMs += m_elapsed.elapsed();
        m_state = RecordingState::Paused;

        m_blinkTimer->stop();
        emit blinkTick(true); // Red dot stays solid during pause
        emit recordingPaused(true);
        qInfo() << "[Capture] Recording suspended";
        return;
    }

    if (m_state == RecordingState::Paused) {
        // Resume: Restart the precision timer
        m_elapsed.restart();
        m_state = RecordingState::Recording;
        m_blinkState = true;

        m_blinkTimer->start(500);
        emit recordingPaused(false);
        qInfo() << "[Capture] Recording resumed";
        return;
    }
}

void CaptureService::doPhotoCapture() {
    m_pendingPhotoPath = StorageManager::instance().requestMediaFilePath(CaptureMode::Photo);
    if (!m_pendingPhotoPath.isEmpty()) {
        m_photoTriggered = true;
    }
}

void CaptureService::startRecording() {
    if (m_state != RecordingState::Idle) return;

    QString videoPath = StorageManager::instance().requestMediaFilePath(CaptureMode::Video);
    if (videoPath.isEmpty()) return;

    StorageManager::instance().setRecordingActive(true);

    QMetaObject::invokeMethod(m_worker, "startVideo", Qt::QueuedConnection,
                              Q_ARG(QString, videoPath));

    m_state = RecordingState::Recording;
    m_accumulatedMs = 0;
    m_blinkState = true;
    m_elapsed.start();

    emit blinkTick(true);
    updateDuration();
    m_blinkTimer->start(500);

    emit recordingStarted();
}

void CaptureService::stopRecording() {
    if (m_state != RecordingState::Recording && m_state != RecordingState::Paused) {
        return;
    }

    StorageManager::instance().setRecordingActive(false);

    QMetaObject::invokeMethod(m_worker, "stopVideo", Qt::QueuedConnection);

    m_state = RecordingState::Stopping;
    m_blinkTimer->stop();
    emit blinkTick(false);
}

void CaptureService::onFrameReady(const VisualFrame& frame) {
    /* Backpressure defense: Drop frame if encoding pipeline is saturated */
    if (m_pendingFrames.loadAcquire() >= MAX_PENDING_FRAMES) {
        qWarning() << "[Capture] Encoding pipeline saturated. Frame dropped.";
        return;
    }

    if (m_photoTriggered) {
        m_pendingFrames.fetchAndAddRelease(1);
        QMetaObject::invokeMethod(m_worker, "processPhoto", Qt::QueuedConnection,
                                  Q_ARG(VisualFrame, frame),
                                  Q_ARG(QString, m_pendingPhotoPath));
        m_photoTriggered = false;
    }
    else if (m_state == RecordingState::Recording) {
        m_pendingFrames.fetchAndAddRelease(1);
        QMetaObject::invokeMethod(m_worker, "processVideoFrame", Qt::QueuedConnection,
                                  Q_ARG(VisualFrame, frame));
    }
}

void CaptureService::onWorkerFrameProcessed() {
    m_pendingFrames.fetchAndSubRelease(1);
}

void CaptureService::onPhotoSaved(const QString& path, bool success) {
    if (!success) {
        qWarning() << "[Capture] Photo save failed:" << path;
        return;
    }

    QString flushError;
    if (!StorageManager::instance().flushMediaPath(path, &flushError)) {
        qWarning() << "[Capture] Photo flush failed:" << path << "reason:" << flushError;
    }
}

void CaptureService::onVideoStopped(const QString& path) {
    if (m_state != RecordingState::Stopping) {
        return;
    }

    if (!path.isEmpty()) {
        QString flushError;
        if (!StorageManager::instance().flushMediaPath(path, &flushError)) {
            qWarning() << "[Capture] Video flush failed:" << path << "reason:" << flushError;
        }
    }

    m_state = RecordingState::Idle;
    emit recordingStopped();
}

void CaptureService::onStorageSpaceCritical() {
    if (m_state != RecordingState::Recording && m_state != RecordingState::Paused) {
        return;
    }

    qWarning() << "[Capture] Storage critical; stopping recording.";
    stopRecording();
}
