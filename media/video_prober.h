#ifndef VIDEO_PROBER_H
#define VIDEO_PROBER_H

#include <QImage>
#include <QString>
#include <QSize>

/**
 * @brief 轻量级的无状态视频媒体探针 (Stateless Video Prober).
 *
 * 专为相册后台扫描设计。提供极速的元数据读取和首帧提取功能，
 * 采用纯静态方法，避免了实例化重量级播放器带来的内存和线程开销。
 */
class VideoProber {
public:
    /**
     * @brief Probes media metadata linearly. Zero decoding overhead.
     * @return Formatted timestamp string (e.g., "02:15").
     */
    static QString getDuration(const QString& path);

    /**
     * @brief Extracts the initial valid keyframe (I-frame) directly to RGB space.
     * Optimizes memory footprint by performing swscale targeting in one pass.
     */
    static QImage extractFirstFrame(const QString& path, const QSize& targetSize = QSize());

private:
    VideoProber() = default;
    ~VideoProber() = default;

};

#endif // VIDEO_PROBER_H
