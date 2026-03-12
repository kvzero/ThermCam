#include "global_context.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

/* Low-level Linux headers strictly kept in implementation file */
#include <fcntl.h>
#include <unistd.h>

GlobalContext& GlobalContext::instance() {
    static GlobalContext inst;
    return inst;
}

GlobalContext::GlobalContext() : m_drmFd(-1), m_isInit(false) {}

GlobalContext::~GlobalContext() {
    if (m_drmFd >= 0) {
        ::close(m_drmFd);
    }
}

bool GlobalContext::init() {
    if (m_isInit) return true;

    /* Primary DRM node for Rockchip display controller */
    m_drmFd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (m_drmFd < 0) {
        qCritical() << "Failed to open DRM device";
        return false;
    }

    /* Screen size is required for RgaAccelerator and UI layouts */
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        m_screenSize = screen->size();
        m_refreshRate = screen->refreshRate();

        qInfo() << "Screen size:" << m_screenSize;
        qInfo() << "Screen geometry:" << screen->geometry();
    } else {
        qCritical() << "Primary screen not detected";
        return false;
    }

    m_isInit = true;
    return true;
}
