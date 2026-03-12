#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <QObject>
#include <QMap>
#include <QPoint>
#include "core/types.h"

class QSocketNotifier;
struct input_absinfo;

/**
 * @brief High-performance hardware abstraction layer for Linux Input Subsystem.
 *
 * This class reads raw bytes from /dev/input nodes and translates them into
 * normalized protocol frames. It does not perform coordinate mapping or logic.
 */
class InputManager : public QObject {
    Q_OBJECT
public:
    explicit InputManager(QObject* parent = nullptr);
    ~InputManager();

    /**
     * @brief Probes device nodes and starts event monitoring asynchronously.
     * @return Always true, ensuring UI rendering is never blocked during fastboot.
     */
    bool init();

private slots:
    void processKeyEvents();
    void processTouchEvents();

private:
    /* Device File Descriptors */
    int m_keyFd = -1;
    int m_touchFd = -1;

    /* Linux Event Notifiers */
    QSocketNotifier* m_keyNotifier = nullptr;
    QSocketNotifier* m_touchNotifier = nullptr;

    /* Calibration Helper */
    void parseEnv();
    QPoint mapToScreen(int rawX, int rawY);
    int m_rotationAngle = 0;

    /* Multi-touch Protocol B State Machine */
    int m_currentSlot = 0;
    QMap<int, RawTouchPoint> m_slots;

    /* Hardware Capability Info (for normalization) */
    struct AbsLimits { int min; int max; };
    AbsLimits m_absX{0, 0}, m_absY{0, 0};

    bool getAbsLimits();
};

#endif // INPUT_MANAGER_H
