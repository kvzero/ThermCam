#include "rga_image.h"
#include "rga_buffer.h"
#include "core/global_context.h"

#include <QMutex>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

#include <rga/im2d.h>
#include <rga/rga.h>

namespace {

// ============================================================
// Backend Context (Meyers' Singleton)
// Manages the shared physical memory scratchpad.
// ============================================================
class RgaSharedContext {
public:
    static RgaSharedContext& instance() {
        static RgaSharedContext inst;
        return inst;
    }

    void ensureInitialized() {
        QMutexLocker lock(&m_initMutex);
        if (m_isReady) return;

        const auto* screen = QGuiApplication::primaryScreen();
        if (!screen) {
            // 使用 qFatal 终止程序，这是最安全的做法
            qFatal("[RGA] CRITICAL ERROR: No primary screen found! "
                    "RGA cannot determine buffer size. Check Qt platform plugin (e.g., EGLFS) or display connection.");
            return; // qFatal a会终止，这里是为了静态分析
        }

        // Align stride to 16 pixels for hardware compatibility
        auto& ctx = GlobalContext::instance();
        m_maxW = (ctx.screenSize().width() + 15) & (~15);
        m_maxH = ctx.screenSize().height();
        m_srcBuffer = new RgaBuffer(ctx.drmFd(), m_maxW, m_maxH);
        m_dstBuffer = new RgaBuffer(ctx.drmFd(), m_maxW, m_maxH);

        if (m_srcBuffer->isValid() && m_dstBuffer->isValid()) {
            m_isReady = true;
            qInfo() << "[RGA] Context Ready. Scratchpad:" << m_maxW << "x" << m_maxH;
        } else {
            qCritical() << "[RGA] Memory Allocation Failed!";
        }
    }

    bool isReady() const { return m_isReady; }

    RgaBuffer* src() { return m_srcBuffer; }
    RgaBuffer* dst() { return m_dstBuffer; }
    int maxW() const { return m_maxW; }
    int maxH() const { return m_maxH; }
    QMutex& workMutex() { return m_workMutex; }

private:
    RgaSharedContext() = default;

    ~RgaSharedContext() {
        delete m_srcBuffer;
        delete m_dstBuffer;
    }

    bool m_isReady = false;
    int m_maxW = 0;
    int m_maxH = 0;

    RgaBuffer* m_srcBuffer = nullptr;
    RgaBuffer* m_dstBuffer = nullptr;

    QMutex m_initMutex;
    QMutex m_workMutex;
};

// ============================================================
// Core Execution Engine
// Logic: Lock -> CPU to RGA Mem -> Hardware Op -> RGA Mem to CPU
// ============================================================
template <typename RgaOpFunc>
QImage execute(const QImage& input, int outW, int outH, RgaOpFunc rgaOp) {
    auto& ctx = RgaSharedContext::instance();

    if (!ctx.isReady()) {
        qWarning() << "[RGA] Not initialized. Call RgaImage::globalInit() first.";
        return input;
    }

    QMutexLocker lock(&ctx.workMutex());

    if (input.width() > ctx.maxW() || input.height() > ctx.maxH() ||
        outW > ctx.maxW() || outH > ctx.maxH()) {
        qWarning() << "[RGA] Image exceeds scratchpad size.";
        return input;
    }

    // 1. Format Normalization
    QImage srcImg = input;
    if (srcImg.format() != QImage::Format_ARGB32 &&
        srcImg.format() != QImage::Format_RGB32) {
        srcImg = srcImg.convertToFormat(QImage::Format_ARGB32);
    }

    // 2. Upload: CPU -> Physical Memory (Input Slot)
    uint8_t* physPtr = (uint8_t*)ctx.src()->vaddr();
    const int physStride = ctx.maxW() * 4;
    const int imgLineBytes = srcImg.width() * 4;
    const uint8_t* srcBits = srcImg.bits();

    for (int y = 0; y < srcImg.height(); ++y) {
        memcpy(physPtr + y * physStride,
               srcBits + y * srcImg.bytesPerLine(),
               imgLineBytes);
    }

    // 3. Hardware Operation
    rga_buffer_t rgaSrc = wrapbuffer_virtualaddr(
            ctx.src()->vaddr(), srcImg.width(), srcImg.height(),
            RK_FORMAT_RGBA_8888, ctx.maxW(), ctx.maxH());

    rga_buffer_t rgaDst = wrapbuffer_virtualaddr(
            ctx.dst()->vaddr(), outW, outH,
            RK_FORMAT_RGBA_8888, ctx.maxW(), ctx.maxH());

    int ret = rgaOp(rgaSrc, rgaDst);
    if (ret != IM_STATUS_SUCCESS) {
        qWarning() << "[RGA] Op Failed:" << ret;
        return input;
    }

    // 4. Download: Physical Memory (Output Slot) -> CPU
    QImage result(outW, outH, QImage::Format_ARGB32);
    physPtr = (uint8_t*)ctx.dst()->vaddr();
    uint8_t* dstBits = result.bits();
    const int resLineBytes = outW * 4;

    for (int y = 0; y < outH; ++y) {
        memcpy(dstBits + y * result.bytesPerLine(),
               physPtr + y * physStride,
               resLineBytes);
    }

    return result;
}

} // anonymous namespace

// ============================================================
// RgaImage Public Implementation
// ============================================================

void RgaImage::globalInit() {
    RgaSharedContext::instance().ensureInitialized();
}

RgaImage::RgaImage(const QImage& image) : m_image(image) {}

QImage RgaImage::toQImage() const {
    return m_image;
}

bool RgaImage::isValid() const {
    return !m_image.isNull();
}

RgaImage RgaImage::scaled(int w, int h) const {
    if (m_image.isNull()) return *this;

    QImage res = execute(m_image, w, h,
        [](rga_buffer_t s, rga_buffer_t d) {
            return imresize(s, d, 0, 0, IM_INTERP_CUBIC);
        });
    return RgaImage(res);
}

RgaImage RgaImage::scaled(const QSize& size) const {
    return scaled(size.width(), size.height());
}

RgaImage RgaImage::blurred(int radius) const {
    if (m_image.isNull()) return *this;

    if (radius % 2 == 0) radius++;

    QImage res = execute(m_image, m_image.width(), m_image.height(),
        [radius](rga_buffer_t s, rga_buffer_t d) {
            return imgaussianBlur(s, d, radius, radius, 0, 0, 1, nullptr);
        });
    return RgaImage(res);
}

RgaImage RgaImage::rotated(int angle) const {
    if (m_image.isNull()) return *this;

    int mode = 0;
    int outW = m_image.width();
    int outH = m_image.height();

    if (angle == 90) {
        outW = m_image.height(); outH = m_image.width();
        mode = IM_HAL_TRANSFORM_ROT_90;
    } else if (angle == 180) {
        mode = IM_HAL_TRANSFORM_ROT_180;
    } else if (angle == 270) {
        outW = m_image.height(); outH = m_image.width();
        mode = IM_HAL_TRANSFORM_ROT_270;
    } else {
        return *this;
    }

    QImage res = execute(m_image, outW, outH,
        [mode](rga_buffer_t s, rga_buffer_t d) {
            return imrotate(s, d, mode);
        });
    return RgaImage(res);
}
