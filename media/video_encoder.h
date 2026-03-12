#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <QString>
#include <QImage>
#include <QElapsedTimer>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    bool open(const QString& filePath, int width, int height);
    bool writeFrame(const QImage& frame);
    void close();

private:
    void cleanup();

    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext*  m_codecCtx  = nullptr;
    AVStream*        m_stream    = nullptr;
    AVFrame*         m_yuvFrame  = nullptr;
    SwsContext*      m_swsCtx    = nullptr;

    QElapsedTimer    m_timer;
    int m_frameCount = 0;
    QString m_currentPath;
};

#endif // VIDEO_ENCODER_H
