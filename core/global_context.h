#ifndef CORE_GLOBAL_CONTEXT_H
#define CORE_GLOBAL_CONTEXT_H

#include <QSize>

/**
 * @brief Global hardware resource and environment provider.
 */
class GlobalContext {
public:
    static GlobalContext& instance();

    /**
     * @brief Initialize DRM device and screen parameters.
     * @return true if both DRM and Screen are ready.
     */
    bool init();

    /* Accessors for shared low-level resources */
    int drmFd() const { return m_drmFd; }
    QSize screenSize() const { return m_screenSize; }
    qreal refreshRate() const { return m_refreshRate; }

private:
    GlobalContext();
    ~GlobalContext();

    GlobalContext(const GlobalContext&) = delete;
    GlobalContext& operator=(const GlobalContext&) = delete;

    int m_drmFd = -1;
    QSize m_screenSize;
    qreal m_refreshRate;
    bool m_isInit = false;
};

#endif // CORE_GLOBAL_CONTEXT_H
