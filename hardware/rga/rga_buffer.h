#ifndef RGA_BUFFER_H
#define RGA_BUFFER_H

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <QImage>
#include <QDebug>

class RgaBuffer {
public:
    RgaBuffer(int drm_fd, int w, int h, int format_bpp = 32)
        : m_fd(drm_fd), m_w(w), m_h(h)
    {
        // 1. Create Dumb Buffer
        struct drm_mode_create_dumb creq = {};
        creq.width = w;
        creq.height = h;
        creq.bpp = format_bpp;
        if (ioctl(m_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
            qCritical() << "RgaBuffer: Failed to create dumb buffer";
            return;
        }

        m_handle = creq.handle;
        m_size = creq.size;
        m_pitch = creq.pitch;

        // 2. Map Dumb Buffer
        struct drm_mode_map_dumb mreq = {};
        mreq.handle = m_handle;
        if (ioctl(m_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
            qCritical() << "RgaBuffer: Failed to map dumb buffer";
            return;
        }

        // 3. Mmap to userspace
        m_vaddr = mmap(0, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, mreq.offset);
        if (m_vaddr == MAP_FAILED) {
            qCritical() << "RgaBuffer: mmap failed";
            m_vaddr = nullptr;
            return;
        }

        // 清空内存，防止花屏
        memset(m_vaddr, 0, m_size);
    }

    ~RgaBuffer() {
        if (m_vaddr) munmap(m_vaddr, m_size);
        if (m_handle) {
            struct drm_mode_destroy_dumb dreq = { m_handle };
            ioctl(m_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
        }
    }

    // --- 状态检查 ---
    bool isValid() const { return m_vaddr != nullptr && m_handle != 0; }

    // --- 关键 Accessors (RGA 驱动需要这些) ---
    uint32_t handle() const { return m_handle; }  // 【新增】给 wrapbuffer_handle 用
    uint32_t stride() const { return m_pitch; }   // 【新增】某些情况需要知道物理步幅
    void* vaddr() const { return m_vaddr; }       // 给 memcpy 用

    // --- 基础属性 ---
    int width() const { return m_w; }
    int height() const { return m_h; }

    // 如果你不需要把 RgaBuffer 直接当 QImage 画，这个其实可以删掉
    // 因为我们的架构里，数据是拷出去变成新 QImage 的
    // const QImage& image() const { return m_image; }

private:
    int m_fd;
    uint32_t m_handle = 0;
    uint32_t m_pitch = 0;
    uint64_t m_size = 0;
    void* m_vaddr = nullptr;
    int m_w, m_h;
};

#endif // RGA_BUFFER_H
