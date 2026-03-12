#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <QThread>
#include <QString>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
}

/**
 * @brief Lightweight software video player engine.
 * Features:
 * - Independent thread for decoding (Non-blocking UI)
 * - Zero-copy rendering via direct QImage memory mapping
 * - Precise A/V sync simulation (System Clock Master)
 * - Seek functionality with buffer flushing
 */
class VideoPlayer : public QThread {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };
    enum class SyncMaster { SystemClock, AudioClock };

    explicit VideoPlayer(QObject* parent = nullptr);
    ~VideoPlayer() override;

    bool open(const QString& path);
    void play();
    void pause();
    void stop();

    /**
     * @brief Seek to a specific timestamp in milliseconds.
     * This is thread-safe and can be called from the UI thread.
     */
    void seek(qint64 targetMs);

    State state() const;
    qint64 durationMs() const;
    qint64 positionMs() const;

signals:
    void frameReady(const QImage& frame);
    void positionChanged(qint64 posMs);
    void playbackFinished();
    void errorOccurred(const QString& errorMsg);

protected:
    void run() override;

private:
    void cleanup();
    double getMasterClock() const;

    /* FFmpeg Contexts */
    AVFormatContext* m_fmtCtx = nullptr;
    AVCodecContext*  m_vCodecCtx = nullptr;
    SwsContext*      m_swsCtx = nullptr;

    int m_videoStreamIdx = -1;
    qint64 m_durationMs = 0;

    /* Thread Control */
    QMutex m_mutex;
    QWaitCondition m_cond;
    State m_state = State::Stopped;
    bool m_abortRequest = false;

    /* Atomic flag for seek requests (-1 means no request) */
    qint64 m_seekTargetMs = -1;

    /* Sync & Timing Engine */
    SyncMaster m_syncMaster = SyncMaster::SystemClock;
    QElapsedTimer m_sysTimer;
    qint64 m_currentPosMs = 0;

    /*
     * m_frameTimer: Tracks the theoretical display time of the last frame.
     * m_timeOffset: Accumulates time during pauses to "freeze" the clock.
     */
    double m_frameTimer = 0.0;
    double m_timeOffset = 0.0;
};

#endif // VIDEO_PLAYER_H
