#include "thermal_processor.h"
#include "hardware/rga/rga_image.h"

ThermalProcessor::ThermalProcessor(QObject *parent) : QObject(parent) {}

void ThermalProcessor::setTargetSize(const QSize& size) {
    m_targetSize = size;
}

void ThermalProcessor::processFrame(const RawFrame& raw) {
    if (m_targetSize.isEmpty() || raw.pixelData.isEmpty()) return;

    // ==========================================
    // 1. 图像处理 (交给 RGA 硬件)
    // ==========================================
    // 构造 QImage 包装器 (无拷贝)
    QImage rawWrapper((const uchar*)raw.pixelData.constData(),
                      raw.w, raw.h, QImage::Format_ARGB32);

    // 链式调用：上传 -> 缩放 -> 下载
    QImage finalImage = RgaImage(rawWrapper)
                            .scaled(m_targetSize)
                            .toQImage();

    // ==========================================
    // 2. 业务逻辑处理 (坐标换算 & 数据封装)
    // ==========================================
    VisualFrame visual;
    visual.image = finalImage; // 填入处理好的图片

    // 计算缩放比例
    float scaleX = (float)m_targetSize.width() / raw.w;
    float scaleY = (float)m_targetSize.height() / raw.h;

    // 映射最高温点
    visual.hot_spot = raw.hot_spot; // 复制温度值
    visual.hot_spot.x = raw.hot_spot.x * scaleX; // 换算坐标
    visual.hot_spot.y = raw.hot_spot.y * scaleY;

    // 映射最低温点
    visual.cold_spot = raw.cold_spot;
    visual.cold_spot.x = raw.cold_spot.x * scaleX;
    visual.cold_spot.y = raw.cold_spot.y * scaleY;    // Proportional coordinate mapping
    float sx = (float)m_targetSize.width() / raw.w;
    float sy = (float)m_targetSize.height() / raw.h;

    auto map = [&](const TempPt& s) {
        return TempPt{ qRound(s.x * sx), qRound(s.y * sy), s.temperature };
    };

    visual.hot_spot    = map(raw.hot_spot);
    visual.cold_spot   = map(raw.cold_spot);
    visual.center_spot = map(raw.center_spot);

    emit frameReady(visual);
}
