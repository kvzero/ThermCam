#include "video_prober.h"
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

QString VideoProber::getDuration(const QString& path) {
    AVFormatContext* fmtCtx = nullptr;

    if (avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return QString();
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return QString();
    }

    QString durationStr;
    if (fmtCtx->duration != AV_NOPTS_VALUE) {
        int64_t durationSec = fmtCtx->duration / AV_TIME_BASE;
        int hours = durationSec / 3600;
        int minutes = (durationSec % 3600) / 60;
        int seconds = durationSec % 60;

        if (hours > 0) {
            durationStr = QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        } else {
            durationStr = QString("%1:%2")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        }
    }

    avformat_close_input(&fmtCtx);
    return durationStr;
}

QImage VideoProber::extractFirstFrame(const QString& path, const QSize& targetSize) {
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return QImage();
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return QImage();
    }

    int videoStreamIdx = -1;
    AVCodecParameters* codecPar = nullptr;
    const AVCodec* codec = nullptr;

    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = static_cast<int>(i);
            codecPar = fmtCtx->streams[i]->codecpar;
            codec = avcodec_find_decoder(codecPar->codec_id);
            break;
        }
    }

    if (videoStreamIdx == -1 || !codec) {
        avformat_close_input(&fmtCtx);
        return QImage();
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return QImage();
    }

    if (codecCtx->pix_fmt == AV_PIX_FMT_NONE || codecCtx->width <= 0 || codecCtx->height <= 0) {
        qWarning() << "[VideoDecoder] Broken file detected, skipping thumbnail generation.";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return QImage();
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    QImage resultImage;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == videoStreamIdx) {
            if (avcodec_send_packet(codecCtx, pkt) == 0) {
                if (avcodec_receive_frame(codecCtx, frame) == 0) {

                    int outW = targetSize.isEmpty() ? codecCtx->width : targetSize.width();
                    int outH = targetSize.isEmpty() ? codecCtx->height : targetSize.height();

                    /*
                     * Passing original pix_fmt preserves color fidelity and enables
                     * hardware-accelerated conversion paths (SIMD/NEON).
                     * Note: "deprecated pixel format" warnings might occur for YUVJ formats,
                     * but this is an acceptable tradeoff for performance and color accuracy.
                     */
                    SwsContext* swsCtx = sws_getContext(
                        codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
                        outW, outH, AV_PIX_FMT_BGRA,
                        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
                    );

                    if (swsCtx) {
                        /*
                         * Strict Memory Alignment Defense:
                         * QImage internally forces 4-byte alignment, which causes SIGBUS on ARM
                         * when sws_scale executes 16/32-byte SIMD instructions.
                         * We allocate a native AVFrame with 32-byte alignment as a staging buffer.
                         */
                        AVFrame* dstFrame = av_frame_alloc();
                        dstFrame->format = AV_PIX_FMT_BGRA;
                        dstFrame->width = outW;
                        dstFrame->height = outH;
                        av_frame_get_buffer(dstFrame, 32);

                        sws_scale(swsCtx, frame->data, frame->linesize, 0, codecCtx->height,
                                  dstFrame->data, dstFrame->linesize);

                        // Safely transfer aligned row data to QImage memory space
                        resultImage = QImage(outW, outH, QImage::Format_ARGB32);
                        for (int y = 0; y < outH; ++y) {
                            memcpy(resultImage.scanLine(y), dstFrame->data[0] + y * dstFrame->linesize[0], outW * 4);
                        }

                        av_frame_free(&dstFrame);
                        sws_freeContext(swsCtx);
                    }

                    // Terminate traversal: We only require the initial keyframe for the thumbnail
                    av_packet_unref(pkt);
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    return resultImage;
}
