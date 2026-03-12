#include "video_player.h"
#include <QDebug>

VideoPlayer::VideoPlayer(QObject* parent) : QThread(parent) {}

VideoPlayer::~VideoPlayer() {
    stop();
    wait();
    cleanup();
}

bool VideoPlayer::open(const QString& path) {
    stop();
    wait();
    cleanup();

    if (avformat_open_input(&m_fmtCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        cleanup();
        return false;
    }

    m_durationMs = m_fmtCtx->duration != AV_NOPTS_VALUE ?
                   (m_fmtCtx->duration * 1000 / AV_TIME_BASE) : 0;

    const AVCodec* vCodec = nullptr;
    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIdx < 0) {
            m_videoStreamIdx = i;
            vCodec = avcodec_find_decoder(m_fmtCtx->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (m_videoStreamIdx < 0 || !vCodec) {
        cleanup();
        return false;
    }

    m_vCodecCtx = avcodec_alloc_context3(vCodec);
    avcodec_parameters_to_context(m_vCodecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

    if (avcodec_open2(m_vCodecCtx, vCodec, nullptr) < 0) {
        cleanup();
        return false;
    }

    if (m_vCodecCtx->pix_fmt == AV_PIX_FMT_NONE || m_vCodecCtx->width <= 0 || m_vCodecCtx->height <= 0) {
        qWarning() << "[VideoPlayer] Blocked corrupted video stream (Invalid pix_fmt or dimensions).";
        cleanup();
        return false;
    }

    m_swsCtx = sws_getContext(
        m_vCodecCtx->width, m_vCodecCtx->height, m_vCodecCtx->pix_fmt,
        m_vCodecCtx->width, m_vCodecCtx->height, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );

    return m_swsCtx != nullptr;
}

void VideoPlayer::play() {
    QMutexLocker lock(&m_mutex);

    if (!m_fmtCtx || !m_vCodecCtx || !m_swsCtx) {
        qWarning() << "[VideoPlayer] Blocked attempt to play corrupted media.";
        emit errorOccurred("FILE CORRUPTED");
        return;
    }

    if (m_state == State::Stopped) {
        m_abortRequest = false;
        m_state = State::Playing;
        m_timeOffset = 0.0;

        if (m_fmtCtx) {
            av_seek_frame(m_fmtCtx, m_videoStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
            if (m_vCodecCtx) {
                avcodec_flush_buffers(m_vCodecCtx);
            }
        }
        start();
    } else if (m_state == State::Paused) {
        m_state = State::Playing;
        m_sysTimer.restart();
        m_cond.wakeOne();
    }
}

void VideoPlayer::pause() {
    QMutexLocker lock(&m_mutex);
    if (m_state == State::Playing) {
        m_timeOffset += m_sysTimer.elapsed() / 1000.0;
        m_state = State::Paused;
    }
}

void VideoPlayer::stop() {
    QMutexLocker lock(&m_mutex);
    m_abortRequest = true;
    m_state = State::Stopped;
    m_timeOffset = 0.0;
    m_cond.wakeOne();
}

void VideoPlayer::seek(qint64 targetMs) {
    QMutexLocker lock(&m_mutex);

    if (!m_fmtCtx || !m_vCodecCtx) return;

    m_seekTargetMs = targetMs;

    if (m_state == State::Paused) {
        m_cond.wakeOne();
    } else if (m_state == State::Stopped) {
        m_abortRequest = false;
        m_state = State::Paused;
        start();
    }
}

VideoPlayer::State VideoPlayer::state() const {
    return m_state;
}

qint64 VideoPlayer::durationMs() const {
    return m_durationMs;
}

qint64 VideoPlayer::positionMs() const {
    return m_currentPosMs;
}

double VideoPlayer::getMasterClock() const {
    if (m_syncMaster == SyncMaster::SystemClock) {
        if (m_state == State::Paused) {
            return m_timeOffset;
        }
        return m_sysTimer.isValid() ? (m_timeOffset + (m_sysTimer.elapsed() / 1000.0)) : 0.0;
    }
    return 0.0;
}

void VideoPlayer::run() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    rgbFrame->format = AV_PIX_FMT_BGRA;
    rgbFrame->width = m_vCodecCtx->width;
    rgbFrame->height = m_vCodecCtx->height;

    // Strict 32-byte alignment to enable NEON SIMD instructions on ARM
    av_frame_get_buffer(rgbFrame, 32);

    m_sysTimer.start();
    bool forceRenderOne = false;

    while (true) {
        {
            QMutexLocker lock(&m_mutex);
            if (m_abortRequest) break;

            if (m_seekTargetMs >= 0) {
                if (m_fmtCtx && m_videoStreamIdx >= 0) {
                    // 1. Time Base Conversion: ms -> stream time_base
                    AVStream* st = m_fmtCtx->streams[m_videoStreamIdx];
                    int64_t targetTs = av_rescale(m_seekTargetMs, st->time_base.den, st->time_base.num * 1000);

                    // 2. Perform Seek
                    // AVSEEK_FLAG_BACKWARD ensures we land on a Keyframe *before* the target,
                    // guaranteeing we can decode forward to the exact frame (no artifacts).
                    if (av_seek_frame(m_fmtCtx, m_videoStreamIdx, targetTs, AVSEEK_FLAG_BACKWARD) >= 0) {

                        // 3. Critical: Flush Decoder Buffers
                        // Clears stale frames from the internal queue. Without this,
                        // you will see 1-2 seconds of "old" video before the jump happens.
                        if (m_vCodecCtx) {
                            avcodec_flush_buffers(m_vCodecCtx);
                        }

                        // 4. Reset Sync Clocks
                        // Re-align the system clock to the new timestamp to prevent
                        // the AVSync logic from thinking we are drastically "behind" or "ahead".
                        m_sysTimer.restart();
                        m_timeOffset = static_cast<double>(m_seekTargetMs) / 1000.0;
                        m_frameTimer = m_timeOffset;
                        m_currentPosMs = m_seekTargetMs;

                        // 5. Notify UI immediately for responsive scrubbing
                        emit positionChanged(m_currentPosMs);
                    }
                }
                // Mark request as consumed
                m_seekTargetMs = -1;
                forceRenderOne = true;
            }

            if (m_state == State::Paused && !forceRenderOne) {
                m_cond.wait(&m_mutex);
                if (m_abortRequest) break;
            }
        }

        if (av_read_frame(m_fmtCtx, pkt) < 0) {
            {
                QMutexLocker lock(&m_mutex);
                m_state = State::Stopped;
            }
            emit playbackFinished();
            break;
        }

        if (pkt->stream_index == m_videoStreamIdx) {
            if (avcodec_send_packet(m_vCodecCtx, pkt) == 0) {
                while (avcodec_receive_frame(m_vCodecCtx, frame) == 0) {

                    double pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? 0 :
                                 frame->best_effort_timestamp * av_q2d(m_fmtCtx->streams[m_videoStreamIdx]->time_base);

                    m_currentPosMs = qRound(pts * 1000);

                    bool isPlaying = false;
                    {
                        QMutexLocker lock(&m_mutex);
                        isPlaying = (m_state == State::Playing);
                    }
                    if (isPlaying) {
                        double diff = pts - getMasterClock();
                        if (diff > 0.01 && diff < 1.0) {
                            av_usleep(static_cast<unsigned int>(diff * 1000000.0));
                        }
                    }

                    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, m_vCodecCtx->height,
                              rgbFrame->data, rgbFrame->linesize);

                    QImage uiImage(rgbFrame->data[0], m_vCodecCtx->width, m_vCodecCtx->height,
                                   rgbFrame->linesize[0], QImage::Format_ARGB32);

                    emit frameReady(uiImage.copy());
                    emit positionChanged(m_currentPosMs);

                    if (forceRenderOne) {
                        QMutexLocker lock(&m_mutex);
                        forceRenderOne = false;
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void VideoPlayer::cleanup() {
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_vCodecCtx) { avcodec_free_context(&m_vCodecCtx); m_vCodecCtx = nullptr; }
    if (m_fmtCtx) { avformat_close_input(&m_fmtCtx); m_fmtCtx = nullptr; }
    m_videoStreamIdx = -1;

    m_durationMs = 0;
    m_currentPosMs = 0;
}
