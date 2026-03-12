#include "video_encoder.h"
#include <QFile>
#include <QDebug>

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {
    close();
}

bool VideoEncoder::open(const QString& filePath, int width, int height) {
    cleanup();

    m_currentPath = filePath;
    m_frameCount = 0;

    // Allocate format context
    if (avformat_alloc_output_context2(&m_formatCtx, nullptr, "avi", filePath.toUtf8().constData()) < 0) {
        return false;
    }

    // Allocate codec & stream
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) return false;

    m_stream = avformat_new_stream(m_formatCtx, codec);
    m_codecCtx = avcodec_alloc_context3(codec);

    m_codecCtx->width = width;
    m_codecCtx->height = height;
    m_codecCtx->time_base = {1, 1000};
    m_stream->time_base = m_codecCtx->time_base;
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    m_codecCtx->qmin = 3;
    m_codecCtx->qmax = 15;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) return false;
    avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx);

    // IO Open
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_formatCtx->pb, filePath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            return false;
        }
    }

    if (avformat_write_header(m_formatCtx, nullptr) < 0) {
        qCritical() << "[VideoEncoder] FATAL: Failed to write AV header!";
        return false;
    }

    // Allocate conversion buffers
    m_yuvFrame = av_frame_alloc();
    m_yuvFrame->format = m_codecCtx->pix_fmt;
    m_yuvFrame->width  = m_codecCtx->width;
    m_yuvFrame->height = m_codecCtx->height;
    av_frame_get_buffer(m_yuvFrame, 32);

    m_swsCtx = sws_getContext(width, height, AV_PIX_FMT_BGRA, // Qt's Format_ARGB32 is actually BGRA
                              width, height, m_codecCtx->pix_fmt,
                              SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    m_timer.restart();
    return true;
}

bool VideoEncoder::writeFrame(const QImage& frame) {
    if (!m_codecCtx || !m_swsCtx) return false;

    m_frameCount++;

    const uint8_t* inData[1] = { frame.constBits() };
    int inLinesize[1] = { static_cast<int>(frame.bytesPerLine()) };

    sws_scale(m_swsCtx, inData, inLinesize, 0, m_codecCtx->height,
              m_yuvFrame->data, m_yuvFrame->linesize);

    m_yuvFrame->pts = m_timer.elapsed();

    if (avcodec_send_frame(m_codecCtx, m_yuvFrame) < 0) return false;

    AVPacket* pkt = av_packet_alloc();
    bool success = true;

    while (avcodec_receive_packet(m_codecCtx, pkt) == 0) {
        av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_stream->time_base);
        pkt->stream_index = m_stream->index;

        if (av_interleaved_write_frame(m_formatCtx, pkt) < 0) {
            success = false;
            break;
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return success;
}

void VideoEncoder::close() {
    bool isGarbageFile = (m_frameCount == 0);

    if (m_formatCtx) {
        // Flush remaining frames in encoder buffer
        avcodec_send_frame(m_codecCtx, nullptr);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(m_codecCtx, pkt) == 0) {
            av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_stream->time_base);
            pkt->stream_index = m_stream->index;
            av_interleaved_write_frame(m_formatCtx, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);

        av_write_trailer(m_formatCtx);
    }
    cleanup();

    if (isGarbageFile && !m_currentPath.isEmpty()) {
        qWarning() << "[VideoEncoder] 0 frames written. Deleting garbage file:" << m_currentPath;
        QFile::remove(m_currentPath);
    }
    m_currentPath.clear();
}

void VideoEncoder::cleanup() {
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_yuvFrame) { av_frame_free(&m_yuvFrame); m_yuvFrame = nullptr; }
    if (m_codecCtx) { avcodec_free_context(&m_codecCtx); m_codecCtx = nullptr; }
    if (m_formatCtx && !(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&m_formatCtx->pb);
    }
    if (m_formatCtx) { avformat_free_context(m_formatCtx); m_formatCtx = nullptr; }
}
