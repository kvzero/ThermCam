#ifndef CAPTURE_SERVICE_H
#define CAPTURE_SERVICE_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QAtomicInt>
#include <QThread>

#include "core/types.h"

class CaptureWorker;

/**
 * @brief Headless service managing media acquisition logic.
 *
 * Acts as the "Brain" for photo and video operations. It coordinates
 * between hardware triggers and UI feedback, handling timing and
 * state transitions independently of the presentation layer.
 */
class CaptureService : public QObject {
    Q_OBJECT
public:
    static CaptureService& instance();

    /** --- Interaction Commands (Input) --- */

    /** @brief Sets the primary operation mode. Only allowed when Idle. */
    void setMode(CaptureMode mode);

    /** @brief Main entry point for the physical Event-Key. */
    void handlePhysicalTrigger();

    /** @brief Toggles between Recording and Paused states. */
    void togglePause();

    /** --- State Accessors --- */
    CaptureMode currentMode() const { return m_mode; }
    RecordingState recordingState() const { return m_state; }

public slots:
    /** @brief Entry point for the raw hardware video stream. */
    void onFrameReady(const VisualFrame& frame);

signals:
    /** --- State Feedback (Output) --- */
    void modeChanged(CaptureMode mode);
    void recordingStarted();
    void recordingStopped();
    void recordingPaused(bool isPaused);

    /** --- UI Synchronization Signals --- */

    /** @brief Fired every 1s during active recording with formatted duration. */
    void durationUpdated(const QString &timeStr);

    /** @brief Heartbeat for the recording indicator (True = Visible). */
    void blinkTick(bool visible);

private slots:
    /** @brief Acknowledgment from worker to release backpressure. */
    void onWorkerFrameProcessed();

private:
    explicit CaptureService(QObject *parent = nullptr);
    ~CaptureService();

    /* Internal Business Logic */
    void startRecording();
    void stopRecording();
    void doPhotoCapture();
    void updateDuration();

    CaptureService(const CaptureService&) = delete;
    CaptureService& operator=(const CaptureService&) = delete;

    /* State Management */
    CaptureMode m_mode = CaptureMode::Photo;
    RecordingState m_state = RecordingState::Idle;

    /* Timing Engine */
    QTimer* m_blinkTimer;        /**< 500ms red-dot blink pulse */
    QElapsedTimer m_elapsed;     /**< Precision timer for current segment */
    qint64 m_accumulatedMs = 0;  /**< Sum of previously recorded segments before pause */
    bool m_blinkState = false;

    /* Concurrency & Backpressure Control */
    static constexpr int MAX_PENDING_FRAMES = 3;

    QThread* m_workerThread = nullptr;
    CaptureWorker* m_worker = nullptr;
    QAtomicInt m_pendingFrames{0};

    QString m_pendingPhotoPath;
    bool m_photoTriggered = false;
};

#endif // CAPTURE_SERVICE_H
